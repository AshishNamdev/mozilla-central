/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Seth Spitzer <sspitzer@netscape.com>
 *   Pierre Phaneuf <pp@ludusdesign.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsAbMDBDirectory.h" 
#include "nsIRDFService.h"
#include "nsRDFCID.h"
#include "nsStringGlue.h"
#include "nsCOMPtr.h"
#include "nsAbBaseCID.h"
#include "nsAddrDatabase.h"
#include "nsIAbMDBCard.h"
#include "nsIAbListener.h"
#include "nsIAbManager.h"
#include "nsIURL.h"
#include "nsNetCID.h"
#include "nsAbDirectoryQuery.h"
#include "nsIAbDirectoryQueryProxy.h"
#include "nsAbQueryStringToExpression.h"
#include "nsIMutableArray.h"
#include "nsArrayEnumerator.h"
#include "nsEnumeratorUtils.h"
#include "mdb.h"
#include "prprf.h"
#include "nsIPrefService.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsILocalFile.h"
#include "nsComponentManagerUtils.h"
#include "nsMemory.h"
#include "nsArrayUtils.h"

nsAbMDBDirectory::nsAbMDBDirectory(void):
     nsAbDirectoryRDFResource(),
     mPerformingQuery(PR_FALSE)
{
  mSearchCache.Init();
}

nsAbMDBDirectory::~nsAbMDBDirectory(void)
{
  if (mDatabase) {
    mDatabase->RemoveListener(this);
  }
}

NS_IMPL_ISUPPORTS_INHERITED5(nsAbMDBDirectory, nsAbDirectoryRDFResource,
                             nsIAbDirectory, nsIAbDirSearchListener,
                             nsIAbMDBDirectory,
                             nsIAbDirectorySearch,
                             nsIAddrDBListener)

NS_IMETHODIMP nsAbMDBDirectory::Init(const char *aUri)
{
  // We need to ensure  that the m_DirPrefId is initialized properly
  nsDependentCString uri(aUri);

  if (uri.Find("MailList") != -1)
    m_IsMailList = PR_TRUE;

  // Mailing lists don't have their own prefs.
  if (m_DirPrefId.IsEmpty() && !m_IsMailList)
  {
    // Find the first ? (of the search params) if there is one.
    // We know we can start at the end of the moz-abmdbdirectory:// because
    // that's the URI we should have been passed.
    PRInt32 searchCharLocation = uri.FindChar('?', kMDBDirectoryRootLen);

    nsCAutoString filename;

    // extract the filename from the uri.
    if (searchCharLocation == -1)
      filename = StringTail(uri, uri.Length() - kMDBDirectoryRootLen);
    else
      filename = Substring(uri, kMDBDirectoryRootLen, searchCharLocation - kMDBDirectoryRootLen);

    // Get the pref servers and the address book directory branch
    nsresult rv;
    nsCOMPtr<nsIPrefService> prefService(do_GetService(NS_PREFSERVICE_CONTRACTID, &rv));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIPrefBranch> prefBranch;
    rv = prefService->GetBranch(NS_LITERAL_CSTRING(PREF_LDAP_SERVER_TREE_NAME ".").get(),
                                getter_AddRefs(prefBranch));
    NS_ENSURE_SUCCESS(rv, rv);

    char** childArray;
    PRUint32 childCount, i;
    PRInt32 dotOffset;
    nsCString childValue;
    nsDependentCString child;

    rv = prefBranch->GetChildList("", &childCount, &childArray);
    NS_ENSURE_SUCCESS(rv, rv);

    for (i = 0; i < childCount; ++i)
    {
      child.Assign(childArray[i]);

      if (StringEndsWith(child, NS_LITERAL_CSTRING(".filename")))
      {
        if (NS_SUCCEEDED(prefBranch->GetCharPref(child.get(),
                                                 getter_Copies(childValue))))
        {
          if (childValue == filename)
          {
            dotOffset = child.RFindChar('.');
            if (dotOffset != -1)
            {
              nsCAutoString prefName(StringHead(child, dotOffset));
              m_DirPrefId.AssignLiteral(PREF_LDAP_SERVER_TREE_NAME ".");
              m_DirPrefId.Append(prefName);
            }
          }
        }
      }
    }     
    NS_FREE_XPCOM_ALLOCATED_POINTER_ARRAY(childCount, childArray);

    NS_ASSERTION(!m_DirPrefId.IsEmpty(),
                 "Error, Could not set m_DirPrefId in nsAbMDBDirectory::Init");
  }

  return nsAbDirectoryRDFResource::Init(aUri);
}

////////////////////////////////////////////////////////////////////////////////

nsresult nsAbMDBDirectory::RemoveCardFromAddressList(nsIAbCard* card)
{
  nsresult rv = NS_OK;
  PRUint32 listTotal;
  PRInt32 i, j;

  // These checks ensure we don't run into null pointers
  // as we did when we caused bug 280463.
  if (!mDatabase)
  {
    rv = GetAbDatabase();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (!m_AddressList)
  {
    rv = mDatabase->GetMailingListsFromDB(this);
    NS_ENSURE_SUCCESS(rv, rv);
    // Ensure that the previous call did set the address list pointer
    if (!m_AddressList)
      return NS_ERROR_NULL_POINTER;
  }

  rv = m_AddressList->Count(&listTotal);
  NS_ENSURE_SUCCESS(rv,rv);

  for (i = listTotal - 1; i >= 0; i--)
  {            
    nsCOMPtr<nsIAbDirectory> listDir(do_QueryElementAt(m_AddressList, i, &rv));
    if (listDir)
    {
      // First remove the instance in the database
      mDatabase->DeleteCardFromMailList(listDir, card, PR_FALSE);

      // Now remove the instance in any lists we hold.
      nsCOMPtr <nsISupportsArray> pAddressLists;
      listDir->GetAddressLists(getter_AddRefs(pAddressLists));
      if (pAddressLists)
      {  
        PRUint32 total;
        rv = pAddressLists->Count(&total);
        for (j = total - 1; j >= 0; j--)
        {
          nsCOMPtr<nsIAbCard> cardInList(do_QueryElementAt(pAddressLists, j, &rv));
          PRBool equals;
          nsresult rv = cardInList->Equals(card, &equals);  // should we checking email?
          if (NS_SUCCEEDED(rv) && equals) {
            pAddressLists->RemoveElementAt(j);
        }
      }
    }
  }
  }
  return NS_OK;
}

NS_IMETHODIMP nsAbMDBDirectory::DeleteDirectory(nsIAbDirectory *directory)
{
  NS_ENSURE_ARG_POINTER(directory);

  nsCOMPtr<nsIAddrDatabase> database;
  nsresult rv = GetDatabase(getter_AddRefs(database));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = database->DeleteMailList(directory, PR_TRUE);

  if (NS_SUCCEEDED(rv))
    database->Commit(nsAddrDBCommitType::kLargeCommit);

  if (m_AddressList)
    m_AddressList->RemoveElement(directory);
  rv = mSubDirectories.RemoveObject(directory);

  NotifyItemDeleted(directory);
  return rv;
}

nsresult nsAbMDBDirectory::NotifyItemChanged(nsISupports *item)
{
  nsresult rv;
  nsCOMPtr<nsIAbManager> abManager = do_GetService(NS_ABMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv,rv);

  rv = abManager->NotifyItemPropertyChanged(item, nsnull, nsnull, nsnull);
  NS_ENSURE_SUCCESS(rv,rv);
  return rv;
}

nsresult nsAbMDBDirectory::NotifyPropertyChanged(nsIAbDirectory *list, const char *property, const PRUnichar* oldValue, const PRUnichar* newValue)
{
  nsresult rv;
  nsCOMPtr<nsISupports> supports = do_QueryInterface(list, &rv);
  NS_ENSURE_SUCCESS(rv,rv);

  nsCOMPtr<nsIAbManager> abManager = do_GetService(NS_ABMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv,rv);

  rv = abManager->NotifyItemPropertyChanged(supports, property, oldValue, newValue);
  NS_ENSURE_SUCCESS(rv,rv);
  return rv;
}

nsresult nsAbMDBDirectory::NotifyItemAdded(nsISupports *item)
{
  nsresult rv = NS_OK;
  nsCOMPtr<nsIAbManager> abManager = do_GetService(NS_ABMANAGER_CONTRACTID, &rv);
  if(NS_SUCCEEDED(rv))
    abManager->NotifyDirectoryItemAdded(this, item);
  return NS_OK;
}

nsresult nsAbMDBDirectory::NotifyItemDeleted(nsISupports *item)
{
  nsresult rv = NS_OK;
  nsCOMPtr<nsIAbManager> abManager = do_GetService(NS_ABMANAGER_CONTRACTID, &rv);
  if(NS_SUCCEEDED(rv))
    abManager->NotifyDirectoryItemDeleted(this, item);

  return NS_OK;
}

// nsIAbMDBDirectory methods

NS_IMETHODIMP nsAbMDBDirectory::ClearDatabase()
{       
  if (mIsQueryURI)
    return NS_ERROR_NOT_IMPLEMENTED;

  if (mDatabase)
  {
    mDatabase->RemoveListener(this);
    mDatabase = nsnull; 
  }
  return NS_OK; 
}

NS_IMETHODIMP nsAbMDBDirectory::RemoveElementsFromAddressList()
{
  if (mIsQueryURI)
    return NS_ERROR_NOT_IMPLEMENTED;

  if (m_AddressList)
  {
    PRUint32 count;
    nsresult rv;
    rv = m_AddressList->Count(&count);
    NS_ASSERTION(NS_SUCCEEDED(rv), "Count failed");
    PRInt32 i;
    for (i = count - 1; i >= 0; i--)
      m_AddressList->RemoveElementAt(i);
  }
  m_AddressList = nsnull;
  return NS_OK;
}

NS_IMETHODIMP nsAbMDBDirectory::RemoveEmailAddressAt(PRUint32 aIndex)
{
  if (mIsQueryURI)
    return NS_ERROR_NOT_IMPLEMENTED;

  if (m_AddressList)
  {
    return m_AddressList->RemoveElementAt(aIndex);
  }
  else
    return NS_ERROR_FAILURE;
}

NS_IMETHODIMP nsAbMDBDirectory::AddDirectory(const char *uriName, nsIAbDirectory **childDir)
{
  if (mIsQueryURI)
    return NS_ERROR_NOT_IMPLEMENTED;

  if (!childDir || !uriName)
    return NS_ERROR_NULL_POINTER;

  if (mURI.IsEmpty())
    return NS_ERROR_NOT_INITIALIZED;

  nsresult rv = NS_OK;
  nsCOMPtr<nsIRDFService> rdf(do_GetService("@mozilla.org/rdf/rdf-service;1", &rv));
  NS_ENSURE_SUCCESS(rv, rv);
  
  nsCOMPtr<nsIRDFResource> res;
  rv = rdf->GetResource(nsDependentCString(uriName), getter_AddRefs(res));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIAbDirectory> directory(do_QueryInterface(res, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  if (mSubDirectories.IndexOf(directory) == -1)
    mSubDirectories.AppendObject(directory);
  NS_IF_ADDREF(*childDir = directory);
  return rv;
}

NS_IMETHODIMP nsAbMDBDirectory::GetDatabaseFile(nsILocalFile **aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);

  nsCString fileName;
  nsresult rv = GetStringValue("filename", EmptyCString(), fileName);
  NS_ENSURE_SUCCESS(rv, rv);

  if (fileName.IsEmpty())
    return NS_ERROR_NOT_INITIALIZED;

  nsCOMPtr<nsIFile> profileDir;
  rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                              getter_AddRefs(profileDir));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = profileDir->AppendNative(fileName);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILocalFile> dbFile = do_QueryInterface(profileDir, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ADDREF(*aResult = dbFile);

  return NS_OK;
}

NS_IMETHODIMP nsAbMDBDirectory::GetDatabase(nsIAddrDatabase **aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);

  nsresult rv;
  nsCOMPtr<nsILocalFile> databaseFile;
  rv = GetDatabaseFile(getter_AddRefs(databaseFile));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIAddrDatabase> addrDBFactory =
    do_GetService(NS_ADDRDATABASE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  return addrDBFactory->Open(databaseFile, PR_FALSE /* no create */, PR_TRUE,
                           aResult);
}

// nsIAbDirectory methods

NS_IMETHODIMP nsAbMDBDirectory::GetURI(nsACString &aURI)
{
  if (mURI.IsEmpty())
    return NS_ERROR_NOT_INITIALIZED;

  aURI = mURI;
  return NS_OK;
}

NS_IMETHODIMP nsAbMDBDirectory::GetChildNodes(nsISimpleEnumerator* *aResult)
{
  if (mIsQueryURI)
    return NS_NewEmptyEnumerator(aResult);

  return NS_NewArrayEnumerator(aResult, mSubDirectories);
}

PR_STATIC_CALLBACK(PLDHashOperator) 
enumerateSearchCache(nsISupports *aKey, nsCOMPtr<nsIAbCard> &aData, void* aClosure)
{
  nsIMutableArray* array = static_cast<nsIMutableArray*>(aClosure);

  array->AppendElement(aData, PR_FALSE);
  return PL_DHASH_NEXT;
}

NS_IMETHODIMP nsAbMDBDirectory::GetChildCards(nsISimpleEnumerator* *result)
{
  nsresult rv;

  if (mIsQueryURI)
  {
    rv = StartSearch();
    NS_ENSURE_SUCCESS(rv, rv);

    // TODO
    // Search is synchronous so need to return
    // results after search is complete
    nsCOMPtr<nsIMutableArray> array(do_CreateInstance(NS_ARRAY_CONTRACTID));
    mSearchCache.Enumerate(enumerateSearchCache, (void*)array);
    return NS_NewArrayEnumerator(result, array);
  }

  rv = GetAbDatabase();

  if (NS_FAILED(rv) || !mDatabase)
    return rv;

  return m_IsMailList ? mDatabase->EnumerateListAddresses(this, result) :
                        mDatabase->EnumerateCards(this, result);
}

NS_IMETHODIMP nsAbMDBDirectory::DeleteCards(nsIArray *aCards)
{
  nsresult rv = NS_OK;

  if (mIsQueryURI) {
    // if this is a query, delete the cards from the directory (without the query)
    // before we do the delete, make this directory (which represents the search)
    // a listener on the database, so that it will get notified when the cards are deleted
    // after delete, remove this query as a listener.
    nsCOMPtr<nsIAddrDatabase> database;
    rv = GetDatabase(getter_AddRefs(database));
    NS_ENSURE_SUCCESS(rv,rv);

    rv = database->AddListener(this);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIRDFResource> resource;
    rv = gRDFService->GetResource(mURINoQuery, getter_AddRefs(resource));
    NS_ENSURE_SUCCESS(rv, rv);
    
    nsCOMPtr<nsIAbDirectory> directory = do_QueryInterface(resource, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = directory->DeleteCards(aCards);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = database->RemoveListener(this);
    NS_ENSURE_SUCCESS(rv, rv);
    return rv;
  }

  if (!mDatabase)
    rv = GetAbDatabase();

  if (NS_SUCCEEDED(rv) && mDatabase)
  {
    PRUint32 cardCount;
    PRUint32 i;
    rv = aCards->GetLength(&cardCount);
    NS_ENSURE_SUCCESS(rv, rv);
    for (i = 0; i < cardCount; i++)
    {
      nsCOMPtr<nsIAbCard> card(do_QueryElementAt(aCards, i, &rv));
      NS_ENSURE_SUCCESS(rv, rv);

      nsCOMPtr<nsIAbMDBCard> dbcard(do_QueryInterface(card, &rv));
      NS_ENSURE_SUCCESS(rv, rv);

      if (card)
      {
        if (m_IsMailList)
        {
          mDatabase->DeleteCardFromMailList(this, card, PR_TRUE);

          PRUint32 cardTotal = 0;
          PRInt32 i;
          if (m_AddressList)
            rv = m_AddressList->Count(&cardTotal);
          for (i = cardTotal - 1; i >= 0; i--)
          {            
            nsCOMPtr<nsIAbMDBCard> dbarrayCard(do_QueryElementAt(m_AddressList, i, &rv));
            if (dbarrayCard)
            {
              PRUint32 tableID, rowID, cardTableID, cardRowID; 
              dbarrayCard->GetDbTableID(&tableID);
              dbarrayCard->GetDbRowID(&rowID);
              dbcard->GetDbTableID(&cardTableID);
              dbcard->GetDbRowID(&cardRowID);
              if (tableID == cardTableID && rowID == cardRowID)
                m_AddressList->RemoveElementAt(i);
            }
          }
        }
        else
        {
          mDatabase->DeleteCard(card, PR_TRUE);
          PRBool bIsMailList = PR_FALSE;
          card->GetIsMailList(&bIsMailList);
          if (bIsMailList)
          {
            //to do, get mailing list dir side uri and notify rdf to remove it
            PRUint32 rowID;
            dbcard->GetDbRowID(&rowID);
            nsCAutoString listUri(mURI);
            listUri.AppendLiteral("/MailList");
            listUri.AppendInt(rowID);
            if (!listUri.IsEmpty())
            {
              nsresult rv = NS_OK;
              nsCOMPtr<nsIRDFService> rdfService = 
                       do_GetService("@mozilla.org/rdf/rdf-service;1", &rv);

              if (NS_FAILED(rv))
                return rv;

              nsCOMPtr<nsIRDFResource> listResource;
              rv = rdfService->GetResource(listUri,
                                           getter_AddRefs(listResource));
              nsCOMPtr<nsIAbDirectory> listDir = do_QueryInterface(listResource, &rv);
              if (NS_FAILED(rv))
                return rv;

              if (m_AddressList)
                m_AddressList->RemoveElement(listDir);

              mSubDirectories.RemoveObject(listDir);

              if (listDir)
                NotifyItemDeleted(listDir);
            }
          }
          else
          { 
            rv = RemoveCardFromAddressList(card);
            NS_ENSURE_SUCCESS(rv,rv);
          }
        }
      }
    }
    mDatabase->Commit(nsAddrDBCommitType::kLargeCommit);
  }
  return rv;
}

NS_IMETHODIMP nsAbMDBDirectory::HasCard(nsIAbCard *cards, PRBool *hasCard)
{
  if(!hasCard)
    return NS_ERROR_NULL_POINTER;

  if (mIsQueryURI)
  {
    *hasCard = mSearchCache.Get(cards, nsnull);
    return NS_OK;
  }

  nsresult rv = NS_OK;
  if (!mDatabase)
    rv = GetAbDatabase();

  if(NS_SUCCEEDED(rv) && mDatabase)
  {
    if(NS_SUCCEEDED(rv))
      rv = mDatabase->ContainsCard(cards, hasCard);
  }
  return rv;
}

NS_IMETHODIMP nsAbMDBDirectory::HasDirectory(nsIAbDirectory *dir, PRBool *hasDir)
{
  if (!hasDir)
    return NS_ERROR_NULL_POINTER;

  nsresult rv;

  nsCOMPtr<nsIAbMDBDirectory> dbdir(do_QueryInterface(dir, &rv));
  NS_ENSURE_SUCCESS(rv, rv);
  
  PRBool bIsMailingList  = PR_FALSE;
  dir->GetIsMailList(&bIsMailingList);
  if (bIsMailingList)
  {
    nsCOMPtr<nsIAddrDatabase> database;
    rv = GetDatabase(getter_AddRefs(database));

    if (NS_SUCCEEDED(rv))
      rv = database->ContainsMailList(dir, hasDir);
  }

  return rv;
}

NS_IMETHODIMP nsAbMDBDirectory::AddMailList(nsIAbDirectory *list)
{
  if (mIsQueryURI)
    return NS_ERROR_NOT_IMPLEMENTED;

  nsresult rv = NS_OK;
  if (!mDatabase)
    rv = GetAbDatabase();

  if (NS_FAILED(rv) || !mDatabase)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIAbMDBDirectory> dblist(do_QueryInterface(list, &rv));
  if (NS_FAILED(rv))
  {
    nsCOMPtr<nsIAbDirectory> newlist(new nsAbMDBDirProperty);
    if (!newlist)
      return NS_ERROR_OUT_OF_MEMORY;

    rv = newlist->CopyMailList(list);
    NS_ENSURE_SUCCESS(rv, rv);

    dblist = do_QueryInterface(newlist, &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    
    mDatabase->CreateMailListAndAddToDB(newlist, PR_TRUE);
  }
  else
    mDatabase->CreateMailListAndAddToDB(list, PR_TRUE);

  mDatabase->Commit(nsAddrDBCommitType::kLargeCommit);

  PRUint32 dbRowID;
  dblist->GetDbRowID(&dbRowID);

  nsCAutoString listUri(mURI);
  listUri.AppendLiteral("/MailList");
  listUri.AppendInt(dbRowID);

  nsCOMPtr<nsIAbDirectory> newList;
  rv = AddDirectory(listUri.get(), getter_AddRefs(newList));
  nsCOMPtr<nsIAbMDBDirectory> dbnewList(do_QueryInterface(newList));
  if (NS_SUCCEEDED(rv) && newList)
  {
    nsCOMPtr<nsIAddrDBListener> listener(do_QueryInterface(newList, &rv));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mDatabase->AddListener(listener);
    NS_ENSURE_SUCCESS(rv, rv);

    dbnewList->CopyDBMailList (dblist);
    AddMailListToDirectory(newList);
    NotifyItemAdded(newList);
  }

  return rv;
}

NS_IMETHODIMP nsAbMDBDirectory::AddCard(nsIAbCard* card, nsIAbCard **addedCard)
{
  if (mIsQueryURI)
    return NS_ERROR_NOT_IMPLEMENTED;

  nsresult rv = NS_OK;
  if (!mDatabase)
    rv = GetAbDatabase();

  if (NS_FAILED(rv) || !mDatabase)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIAbCard> newCard;
  nsCOMPtr<nsIAbMDBCard> dbcard;

  dbcard = do_QueryInterface(card, &rv);
  if (NS_FAILED(rv) || !dbcard) {
    dbcard = do_CreateInstance(NS_ABMDBCARD_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

    newCard = do_QueryInterface(dbcard, &rv);
    NS_ENSURE_SUCCESS(rv,rv);
  
    rv = newCard->Copy(card);
  NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    newCard = card;
  }

  dbcard->SetAbDatabase (mDatabase);
  if (m_IsMailList)
    mDatabase->CreateNewListCardAndAddToDB(this, m_dbRowID, newCard, PR_TRUE /* notify */);
  else
    mDatabase->CreateNewCardAndAddToDB(newCard, PR_TRUE);
  mDatabase->Commit(nsAddrDBCommitType::kLargeCommit);

  NS_IF_ADDREF(*addedCard = newCard);
  return NS_OK;
}

NS_IMETHODIMP nsAbMDBDirectory::ModifyCard(nsIAbCard *aModifiedCard)
{
  NS_ENSURE_ARG_POINTER(aModifiedCard);

  nsresult rv;
  if (!mDatabase)
  {
    rv = GetAbDatabase();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = mDatabase->EditCard(aModifiedCard, PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);
  return mDatabase->Commit(nsAddrDBCommitType::kLargeCommit);
}

NS_IMETHODIMP nsAbMDBDirectory::DropCard(nsIAbCard* aCard, PRBool needToCopyCard)
{
  NS_ENSURE_ARG_POINTER(aCard);

  if (mIsQueryURI)
    return NS_ERROR_NOT_IMPLEMENTED;

  nsresult rv = NS_OK;

  if (!mDatabase)
    rv = GetAbDatabase();

  if (NS_FAILED(rv) || !mDatabase)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIAbCard> newCard;
  nsCOMPtr<nsIAbMDBCard> dbcard;

  if (needToCopyCard) {
    dbcard = do_CreateInstance(NS_ABMDBCARD_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

    newCard = do_QueryInterface(dbcard, &rv);
    NS_ENSURE_SUCCESS(rv,rv);
  
    rv = newCard->Copy(aCard);
  NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    dbcard = do_QueryInterface(aCard, &rv);
    NS_ENSURE_SUCCESS(rv,rv);
    newCard = aCard;
  }  

  dbcard->SetAbDatabase(mDatabase);

  if (m_IsMailList) {
    if (needToCopyCard) {
      nsCOMPtr <nsIMdbRow> cardRow;
      // if card doesn't exist in db, add the card to the directory that 
      // contains the mailing list.
      mDatabase->FindRowByCard(newCard, getter_AddRefs(cardRow));
      if (!cardRow)
        mDatabase->CreateNewCardAndAddToDB(newCard, PR_TRUE /* notify */);
      else
        mDatabase->InitCardFromRow(newCard, cardRow);
    }
    // since we didn't copy the card, we don't have to notify that it was inserted
    mDatabase->CreateNewListCardAndAddToDB(this, m_dbRowID, newCard, PR_FALSE /* notify */);
  }
  else {
    mDatabase->CreateNewCardAndAddToDB(newCard, PR_TRUE /* notify */);
  }
  mDatabase->Commit(nsAddrDBCommitType::kLargeCommit);
  return NS_OK;
}

NS_IMETHODIMP nsAbMDBDirectory::EditMailListToDatabase(nsIAbCard *listCard)
{
  if (mIsQueryURI)
    return NS_ERROR_NOT_IMPLEMENTED;

  if (!m_IsMailList)
    return NS_ERROR_UNEXPECTED;

  nsresult rv = GetAbDatabase();
  NS_ENSURE_SUCCESS(rv, rv);

  mDatabase->EditMailList(this, listCard, PR_TRUE);
  mDatabase->Commit(nsAddrDBCommitType::kLargeCommit);

  return NS_OK;
}

// nsIAddrDBListener methods

NS_IMETHODIMP nsAbMDBDirectory::OnCardAttribChange(PRUint32 abCode)
{
  return NS_OK;
}

NS_IMETHODIMP nsAbMDBDirectory::OnCardEntryChange
(PRUint32 abCode, nsIAbCard *card)
{
  NS_ENSURE_ARG_POINTER(card);
  nsCOMPtr<nsISupports> cardSupports(do_QueryInterface(card));
  nsresult rv;

  switch (abCode) {
  case AB_NotifyInserted:
    rv = NotifyItemAdded(cardSupports);
    break;
  case AB_NotifyDeleted:
    rv = NotifyItemDeleted(cardSupports);
    break;
  case AB_NotifyPropertyChanged:
    rv = NotifyItemChanged(cardSupports);
    break;
  default:
    rv = NS_ERROR_UNEXPECTED;
    break;
  }
    
  NS_ENSURE_SUCCESS(rv, rv);
  return rv;
}

NS_IMETHODIMP nsAbMDBDirectory::OnListEntryChange
(PRUint32 abCode, nsIAbDirectory *list)
{
  nsresult rv = NS_OK;
  
  if (abCode == AB_NotifyPropertyChanged && list)
  {
    PRBool bIsMailList = PR_FALSE;
    rv = list->GetIsMailList(&bIsMailList);
    NS_ENSURE_SUCCESS(rv,rv);
    
    nsCOMPtr<nsIAbMDBDirectory> dblist(do_QueryInterface(list, &rv));
    NS_ENSURE_SUCCESS(rv,rv);

    if (bIsMailList) {
      nsString listName;
      rv = list->GetDirName(listName);
      NS_ENSURE_SUCCESS(rv,rv);

      rv = NotifyPropertyChanged(list, "DirName", nsnull, listName.get());
      NS_ENSURE_SUCCESS(rv,rv);
    }
  }
  return rv;
}

NS_IMETHODIMP nsAbMDBDirectory::OnAnnouncerGoingAway()
{
  if (mDatabase)
      mDatabase->RemoveListener(this);
  return NS_OK;
}

// nsIAbDirectorySearch methods

NS_IMETHODIMP nsAbMDBDirectory::StartSearch()
{
  if (!mIsQueryURI)
    return NS_ERROR_FAILURE;

  nsresult rv;

  mPerformingQuery = PR_TRUE;
  mSearchCache.Clear();

  nsCOMPtr<nsIAbDirectoryQueryArguments> arguments = do_CreateInstance(NS_ABDIRECTORYQUERYARGUMENTS_CONTRACTID,&rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIAbBooleanExpression> expression;
  rv = nsAbQueryStringToExpression::Convert(mQueryString.get(),
    getter_AddRefs(expression));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = arguments->SetExpression(expression);
  NS_ENSURE_SUCCESS(rv, rv);

  // don't search the subdirectories 
  // if the current directory is a mailing list, it won't have any subdirectories
  // if the current directory is a addressbook, searching both it
  // and the subdirectories (the mailing lists), will yield duplicate results
  // because every entry in a mailing list will be an entry in the parent addressbook
  rv = arguments->SetQuerySubDirectories(PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  // Get the directory without the query
  nsCOMPtr<nsIRDFResource> resource;
  rv = gRDFService->GetResource(mURINoQuery, getter_AddRefs(resource));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIAbDirectory> directory(do_QueryInterface(resource, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  // Initiate the proxy query with the no query directory
  nsCOMPtr<nsIAbDirectoryQueryProxy> queryProxy = 
      do_CreateInstance(NS_ABDIRECTORYQUERYPROXY_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = queryProxy->Initiate();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = queryProxy->DoQuery(directory, arguments, this, -1, 0, &mContext);
  return NS_OK;
}

NS_IMETHODIMP nsAbMDBDirectory::StopSearch()
{
  if (!mIsQueryURI)
    return NS_ERROR_FAILURE;

  return NS_OK;
}


// nsAbDirSearchListenerContext methods

NS_IMETHODIMP nsAbMDBDirectory::OnSearchFinished(PRInt32 aResult,
                                                 const nsAString &aErrorMsg)
{
  mPerformingQuery = PR_FALSE;
  return NS_OK;
}

NS_IMETHODIMP nsAbMDBDirectory::OnSearchFoundCard(nsIAbCard* card)
{
  mSearchCache.Put(card, card);

  // TODO
  // Search is synchronous so asserting on the
  // datasource will not work since the getChildCards
  // method will not have returned with results.
  // NotifyItemAdded (card);
  return NS_OK;
}

nsresult nsAbMDBDirectory::GetAbDatabase()
{
  if (mURI.IsEmpty())
    return NS_ERROR_NOT_INITIALIZED;

  if (mDatabase)
    return NS_OK;

  nsresult rv;

  if (m_IsMailList)
  {
    // Get the database of the parent directory.
    nsCString parentURI(mURINoQuery);

    PRInt32 pos = parentURI.RFindChar('/');

    // If we didn't find a / something really bad has happened
    if (pos == -1)
      return NS_ERROR_FAILURE;

    parentURI = StringHead(parentURI, pos);

    nsCOMPtr<nsIRDFService> rdfService =
      do_GetService("@mozilla.org/rdf/rdf-service;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIRDFResource> resource;
    rv = rdfService->GetResource(parentURI, getter_AddRefs(resource));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIAbMDBDirectory> mdbDir(do_QueryInterface(resource, &rv));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mdbDir->GetDatabase(getter_AddRefs(mDatabase));
  }
  else
    rv = GetDatabase(getter_AddRefs(mDatabase));

  if (NS_SUCCEEDED(rv))
    rv = mDatabase->AddListener(this);

  return rv;
}

NS_IMETHODIMP nsAbMDBDirectory::CardForEmailAddress(const nsACString &aEmailAddress, nsIAbCard ** aAbCard)
{
  NS_ENSURE_ARG_POINTER(aAbCard);

  *aAbCard = NULL;

  // Ensure that if we've not been given an email address we never match
  // so that we don't fail out unnecessarily and we don't match a blank email
  // address against random cards that the user hasn't supplied an email for.
  if (aEmailAddress.IsEmpty())
    return NS_OK;

  nsresult rv = NS_OK;
  if (!mDatabase)
    rv = GetAbDatabase();
  if (rv == NS_ERROR_FILE_NOT_FOUND)
  {
    // If file wasn't found, the card cannot exist.
    return NS_OK;
  }
  NS_ENSURE_SUCCESS(rv, rv);

  mDatabase->GetCardFromAttribute(this, kLowerPriEmailColumn /* see #196777 */, aEmailAddress, PR_TRUE /* caseInsensitive, see bug #191798 */, aAbCard);
  if (!*aAbCard) 
  {
    // fix for bug #187239
    // didn't find it as the primary email?  try again, with k2ndEmailColumn ("Additional Email")
    // 
    // TODO bug #198731
    // unlike the kPriEmailColumn, we don't have kLower2ndEmailColumn
    // so we will still suffer from bug #196777 for "additional emails"
    mDatabase->GetCardFromAttribute(this, k2ndEmailColumn, aEmailAddress, PR_TRUE /* caseInsensitive, see bug #191798 */, aAbCard);
  }

  return NS_OK;
}

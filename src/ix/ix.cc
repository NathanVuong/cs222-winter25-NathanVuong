#include "src/include/ix.h"

#include <sstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <utility>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <dirent.h>
#include <iostream>
#include <fstream>
#include <cassert>
#include <iterator>
#include <stdexcept>
#include <memory>

namespace PeterDB {
    IndexManager &IndexManager::instance() {
        static IndexManager _index_manager = IndexManager();
        return _index_manager;
    }

    // Initialize file and append two pages: a root leaf node page, and before that, a page that identifies that page num as the root node
    RC IndexManager::createFile(const std::string &fileName) {
        PagedFileManager &pfm = PagedFileManager::instance();
        return pfm.createFile(fileName);
    }

    RC IndexManager::initializeIndex(IXFileHandle &ixFileHandle) {
        // Check if file exists and its size
        if (ixFileHandle.PFHandle == nullptr || ixFileHandle.PFHandle->getNumberOfPages() >= 1) {
            std::cerr << "IndexManager::initializeIndex: Fail.\n";
            return -1;
        }

        // Append page that contains the page number of the root node which starts at page 1
        char zeroPage[PAGE_SIZE] = {0};
        unsigned rootPageNum = 1;
        memcpy(zeroPage, &rootPageNum, sizeof(unsigned));
        ixFileHandle.PFHandle->appendPage(zeroPage);

        // Append root leaf node page
        char newPage[PAGE_SIZE] = {0};
        unsigned isLeafBit = 1;
        unsigned numEntries = 0;
        unsigned parentPageNum = 0; // Root has no parent
        unsigned siblingPage = 0;   // Root has no sibling

        memcpy(newPage, &isLeafBit, sizeof(unsigned));
        memcpy(newPage + sizeof(unsigned), &numEntries, sizeof(unsigned));
        memcpy(newPage + 2 * sizeof(unsigned), &parentPageNum, sizeof(unsigned));
        memcpy(newPage + PAGE_SIZE - sizeof(unsigned), &siblingPage, sizeof(unsigned));

        ixFileHandle.PFHandle->appendPage(newPage);
        this->initialized = true;
        return 0;
    }


    RC IndexManager::destroyFile(const std::string &fileName) {
        PagedFileManager &pfm = PagedFileManager::instance();
        if (pfm.destroyFile(fileName) == -1) {
            return -1;
        }
        return 0;
    }

    RC IndexManager::openFile(const std::string &fileName, IXFileHandle &ixFileHandle) {
        if (ixFileHandle.fileHandleSet) {
            return -1;
        }

        PagedFileManager &pfm = PagedFileManager::instance();
        ixFileHandle.PFHandle = new FileHandle();
        if (pfm.openFile(fileName, *ixFileHandle.PFHandle) == -1) {
            std::cerr << "Error opening file " << fileName << std::endl;
            return -1;
        }
        ixFileHandle.fileHandleSet = true;
        return 0;
    }

    RC IndexManager::closeFile(IXFileHandle &ixFileHandle) {
        PagedFileManager &pfm = PagedFileManager::instance();
        if (pfm.closeFile(*ixFileHandle.PFHandle) == -1) {
            std::cerr << "Error closing file" << std::endl;
            return -1;
        }
        ixFileHandle.fileHandleSet = false;
        delete ixFileHandle.PFHandle;
        ixFileHandle.PFHandle = nullptr;
        return 0;
    }

    // Retrieve root page number
    unsigned IndexManager::getRootPageNum(IXFileHandle &ixFileHandle) {
        char zeroPage[PAGE_SIZE] = {0};
        if (ixFileHandle.PFHandle->readPage(0, zeroPage) == -1) {
            std::cerr << "Error reading root page" << std::endl;
            return -1;
        }
        unsigned rootPageNum;
        memcpy(&rootPageNum, zeroPage, sizeof(unsigned));
        return rootPageNum;
    }

    // Set root page number
    RC IndexManager::setRootPageNum(IXFileHandle &ixFileHandle, unsigned newRootPageNum) {
        char zeroPage[PAGE_SIZE] = {0};
        if (ixFileHandle.PFHandle->readPage(0, zeroPage) == -1) {
            std::cerr << "Error reading root page" << std::endl;
            return -1;
        }
        memcpy(zeroPage, &newRootPageNum, sizeof(unsigned));
        if (ixFileHandle.PFHandle->writePage(0, zeroPage) == -1) {
            std::cerr << "Error writing root page" << std::endl;
            return -1;
        }
        return 0;
    }

    // Check if a page is a leaf node using page data
    bool IndexManager::isLeafNode(const char *pageData) {
        unsigned isLeafBit;
        memcpy(&isLeafBit, pageData, sizeof(unsigned));
        return isLeafBit == 1;
    }

    // Get the number of entries in a page using page data
    unsigned IndexManager::getNumEntries(const char *pageData) {
        unsigned numEntries;
        memcpy(&numEntries, pageData + sizeof(unsigned), sizeof(unsigned));
        return numEntries;
    }

    // Get the parent page number from page data
    unsigned IndexManager::getParentPageNum(const char *pageData) {
        unsigned parentPageNum;
        memcpy(&parentPageNum, pageData + 2 * sizeof(unsigned), sizeof(unsigned));
        return parentPageNum;
    }

    unsigned IndexManager::getLeafPageByKey(IXFileHandle &ixFileHandle, const Attribute &attribute, const void *key) {
        // Open root node
        unsigned rootPageNum = getRootPageNum(ixFileHandle);
        unsigned targetPageNum = rootPageNum;
        char pageData[PAGE_SIZE];
        ixFileHandle.PFHandle->readPage(rootPageNum, pageData);
        ixFileHandle.PFHandle->readPageCounter -= 1;

        // Iterate through internal nodes to the leaf
        while (!isLeafNode(pageData)) {
            bool foundTargetBeforeLast = false;
            // Skip past metadata
            char* pageDataPtr = pageData + PAGE_METADATA;

            // Iterate through all entries to find entry where our given key is smaller, we must go to its left child
            unsigned numEntries = getNumEntries(pageData);
            for (unsigned i = 0; i < numEntries; i++) {
                // Start by skipping over left child page number, check condition then skip past that entry before repeating
                pageDataPtr += sizeof(unsigned);
                if (attribute.type == TypeInt) {
                    int currentKey;
                    memcpy(&currentKey, pageDataPtr, sizeof(int));
                    if (*(int *)key < currentKey) {
                        memcpy(&targetPageNum, pageDataPtr - sizeof(unsigned), sizeof(unsigned));
                        ixFileHandle.PFHandle->readPage(targetPageNum, pageData);
                        foundTargetBeforeLast = true;
                        break;
                    }
                    pageDataPtr += sizeof(unsigned);
                }
                else if (attribute.type == TypeReal) {
                    float currentKey;
                    memcpy(&currentKey, pageDataPtr, sizeof(float));

                    if (*(float *)key < currentKey) {
                        memcpy(&targetPageNum, pageDataPtr - sizeof(unsigned), sizeof(unsigned));
                        ixFileHandle.PFHandle->readPage(targetPageNum, pageData);
                        foundTargetBeforeLast = true;
                        break;
                    }
                    pageDataPtr += sizeof(unsigned);
                }
                else if (attribute.type == TypeVarChar) {
                    unsigned entryStrLen;
                    memcpy(&entryStrLen, pageDataPtr, sizeof(unsigned));
                    std::string entryStr(pageDataPtr + sizeof(unsigned), entryStrLen);

                    unsigned keyStrLen;
                    memcpy(&keyStrLen, key, sizeof(unsigned));
                    const char *keyCharData = reinterpret_cast<const char *>(key) + sizeof(unsigned);
                    std::string keyStr(keyCharData, keyStrLen);

                    if (keyStr < entryStr) {
                        memcpy(&targetPageNum, pageDataPtr - sizeof(unsigned), sizeof(unsigned));
                        ixFileHandle.PFHandle->readPage(targetPageNum, pageData);
                        foundTargetBeforeLast = true;
                        break;
                    }
                    pageDataPtr += sizeof(unsigned) + entryStrLen;
                }
            }
            if (!foundTargetBeforeLast) {
                // In the case where we must go to the very right node
                memcpy(&targetPageNum, pageDataPtr, sizeof(unsigned));
                ixFileHandle.PFHandle->readPage(targetPageNum, pageData);
                ixFileHandle.PFHandle->readPageCounter -= 1;
            }
        }
        return targetPageNum;
    }


    // Help insert entry at a certain point in a leaf
    void IndexManager::insertEntryIntoLeaf(char *pageData, unsigned spotToInsert, const Attribute &attribute, const void *key, const RID &rid, bool keyAlreadyExists, unsigned spaceTaken) {
        char *pageDataPtr = pageData + spotToInsert;
        if (keyAlreadyExists) {
            if (attribute.type == TypeInt || attribute.type == TypeReal) {
                pageDataPtr += sizeof(unsigned);
                // Retrieve number of RIDs before overwriting it
                unsigned numRIDs;
                memcpy(&numRIDs, pageDataPtr, sizeof(unsigned));
                unsigned updatedNumRIDs = numRIDs + 1;
                memcpy(pageDataPtr, &updatedNumRIDs, sizeof(unsigned));
                pageDataPtr += sizeof(unsigned) + (sizeof(unsigned) * 2 * numRIDs);
                // Move everything to the right of pageDataPtr over by 8, remember not to touch sibling page number
                unsigned bytesToMove = (spaceTaken - 4) - (spotToInsert + sizeof(unsigned) * 2 + numRIDs * sizeof(unsigned) * 2);
                memmove(pageDataPtr + 8, pageDataPtr, bytesToMove);
                // Fill in RID
                memcpy(pageDataPtr, &rid.pageNum, sizeof(unsigned));
                memcpy(pageDataPtr + sizeof(unsigned), &rid.slotNum, sizeof(unsigned));
            } else if (attribute.type == TypeVarChar) {
                unsigned entryStrLen;
                memcpy(&entryStrLen, pageDataPtr, sizeof(unsigned));
                pageDataPtr += sizeof(unsigned) + entryStrLen;
                // Retrieve number of RIDs before overwriting it
                unsigned numRIDs;
                memcpy(&numRIDs, pageDataPtr, sizeof(unsigned));
                unsigned updatedNumRIDs = numRIDs + 1;
                memcpy(pageDataPtr, &updatedNumRIDs, sizeof(unsigned));
                pageDataPtr += sizeof(unsigned) + (sizeof(unsigned) * 2 * numRIDs);
                // Move everything to the right of pageDataPtr over by 8, remember not to touch sibling page number
                unsigned bytesToMove = (spaceTaken - 4) - (spotToInsert + sizeof(unsigned) + entryStrLen + sizeof(unsigned) + numRIDs * sizeof(unsigned) * 2);
                memmove(pageDataPtr + 8, pageDataPtr, bytesToMove);
                // Fill in RID
                memcpy(pageDataPtr, &rid.pageNum, sizeof(unsigned));
                memcpy(pageDataPtr + sizeof(unsigned), &rid.slotNum, sizeof(unsigned));
            }
        } else {
            // Update number of keys in metadata
            unsigned numEntries;
            memcpy(&numEntries, pageData + sizeof(unsigned), sizeof(unsigned));
            unsigned newNumEntries = numEntries + 1;
            memcpy(pageData + sizeof(unsigned), &newNumEntries, sizeof(unsigned));

            // Remember to set how many records
            unsigned numberOfEntriesIsOne = 1;
            // Bytes to move
            unsigned bytesToMove = (spaceTaken - 4) - spotToInsert;

            if (attribute.type == TypeInt || attribute.type == TypeReal) {
                // Move everything to the right
                memmove(pageDataPtr + sizeof(unsigned) * 4, pageDataPtr, bytesToMove);
                // Copy data in
                memcpy(pageDataPtr, key, sizeof(unsigned));
                memcpy(pageDataPtr + sizeof(unsigned), &numberOfEntriesIsOne, sizeof(unsigned));
                memcpy(pageDataPtr + sizeof(unsigned) * 2, &rid.pageNum, sizeof(unsigned));
                memcpy(pageDataPtr + sizeof(unsigned) * 3, &rid.slotNum, sizeof(unsigned));
            } else if (attribute.type == TypeVarChar) {
                // Move everything to the right, calculate key length and string value
                unsigned keyStrLen;
                memcpy(&keyStrLen, key, sizeof(unsigned));
                const char *keyCharData = reinterpret_cast<const char *>(key) + sizeof(unsigned);
                memmove(pageDataPtr + sizeof(unsigned) * 4 + keyStrLen, pageDataPtr, bytesToMove);
                // Copy data in
                memcpy(pageDataPtr, &keyStrLen, sizeof(unsigned));
                memcpy(pageDataPtr + sizeof(unsigned), keyCharData, keyStrLen);
                memcpy(pageDataPtr + sizeof(unsigned) + keyStrLen, &numberOfEntriesIsOne, sizeof(unsigned));
                memcpy(pageDataPtr + sizeof(unsigned) * 2 + keyStrLen, &rid.pageNum, sizeof(unsigned));
                memcpy(pageDataPtr + sizeof(unsigned) * 3 + keyStrLen, &rid.slotNum, sizeof(unsigned));
            }
        }
    }

    void IndexManager::insertEntryIntoInternal(char *pageData, const Attribute &attribute, const void *key, unsigned leftPageNum, unsigned rightPageNum) {
        unsigned numKeys = getNumEntries(pageData);

        char *currentPtr = pageData + PAGE_METADATA + sizeof(unsigned);
        char *spotToInsert;
        bool spotToInsertFound = false;
        // Locate the position to insert
        for (unsigned i = 0; i < numKeys; i++) {
            if (attribute.type == TypeInt) {
                int existingKey;
                memcpy(&existingKey, currentPtr, sizeof(int));
                if (!spotToInsertFound) {
                    if (*reinterpret_cast<const int *>(key) < existingKey) {
                        spotToInsert = currentPtr;
                        spotToInsertFound = true;
                    }
                }
                currentPtr += sizeof(unsigned) * 2;
            } else if (attribute.type == TypeReal) {
                float existingKey;
                memcpy(&existingKey, currentPtr, sizeof(float));
                if (!spotToInsertFound) {
                    if (*reinterpret_cast<const float *>(key) < existingKey) {
                        spotToInsert = currentPtr;
                        spotToInsertFound = true;
                    }
                }
                currentPtr += sizeof(unsigned) * 2;
            } else if (attribute.type == TypeVarChar) {
                unsigned keyLen;
                memcpy(&keyLen, currentPtr, sizeof(unsigned));
                std::string existingKey(reinterpret_cast<char *>(currentPtr + sizeof(unsigned)), keyLen);
                unsigned inputKeyLen;
                memcpy(&inputKeyLen, key, sizeof(unsigned));
                std::string inputKey(reinterpret_cast<const char *>(key) + sizeof(unsigned), inputKeyLen);
                if (!spotToInsertFound) {
                    if (inputKey < existingKey) {
                        spotToInsert = currentPtr;
                        spotToInsertFound = true;
                    }
                }
                currentPtr += sizeof(unsigned) + sizeof(unsigned) + keyLen;
            }
        }

        if (!spotToInsertFound) {
            spotToInsert = currentPtr;
        }

        // Compute bytes to move
        unsigned bytesToMove = (currentPtr - pageData) - (spotToInsert - pageData);

        // Shift data right to make space for new entry
        if (attribute.type == TypeInt || attribute.type == TypeReal) {
            memmove(spotToInsert + sizeof(unsigned) * 2, spotToInsert, bytesToMove);
            memcpy(spotToInsert, key, sizeof(unsigned));
            memcpy(spotToInsert + sizeof(unsigned), &rightPageNum, sizeof(unsigned));
        } else if (attribute.type == TypeVarChar) {
            unsigned keyLen;
            memcpy(&keyLen, key, sizeof(unsigned));
            memmove(spotToInsert + sizeof(unsigned) + sizeof(unsigned) + keyLen, spotToInsert, bytesToMove);
            memcpy(spotToInsert, &keyLen, sizeof(unsigned));
            memcpy(spotToInsert + sizeof(unsigned), reinterpret_cast<const char *>(key) + sizeof(unsigned), keyLen);
            memcpy(spotToInsert + sizeof(unsigned) + keyLen, &rightPageNum, sizeof(unsigned));
        }

        // Update the number of keys in the metadata
        unsigned newNumKeys = numKeys + 1;
        memcpy(pageData + sizeof(unsigned), &newNumKeys, sizeof(unsigned));
    }

    // For intenal nodes
    unsigned IndexManager::calculateInternalSpaceTaken(char *pageData, const Attribute &attribute) {
        unsigned numEntries = getNumEntries(pageData);

        char *currentPtr = pageData + PAGE_METADATA;
        unsigned spaceTaken = PAGE_METADATA;

        // Internal node structure: pageNum | key | pageNum | key | pageNum
        spaceTaken += sizeof(unsigned);
        currentPtr += sizeof(unsigned);
        for (unsigned i = 0; i < numEntries; i++) {
            if (attribute.type == TypeInt || attribute.type == TypeReal) {
                spaceTaken += sizeof(unsigned) * 2; // Key + pageNum
                currentPtr += sizeof(unsigned) * 2;
            } else if (attribute.type == TypeVarChar) {
                unsigned keyLen;
                memcpy(&keyLen, currentPtr, sizeof(unsigned));
                spaceTaken += sizeof(unsigned) + keyLen + sizeof(unsigned); // KeyLen + Key + pageNum
                currentPtr += sizeof(unsigned) + keyLen + sizeof(unsigned);
            }
        }
        return spaceTaken;
    }

    /*
    1. Open the root node page
    2. While the current page is an internal node, iterate through the node to find the next page/node to open
    3. When you find a leaf node, check if there is space for the record, consider that a key might already exist
       If you can insert:
       Just insert it, marking the key down if needed or just appending it to an existing key with RIDs
       If you can't insert apply splitting logic:
       You will need to make a separate page, appending the bottom half of the data of the current page into it
       Then you will have space to insert the current record
       Grab the parent page number and insert the first record from the newly created page into it if possible
       If there is space just insert and set the right pointer of that inserted entry to the new page
       If not split the parent page into two, but take the middle record to push to another internal node, append another root if needed
    */
    RC
    IndexManager::insertEntry(IXFileHandle &ixFileHandle, const Attribute &attribute, const void *key, const RID &rid) {
        // Create root and root pointer if needed
        if (ixFileHandle.initialized == false) {
            initializeIndex(ixFileHandle);
            ixFileHandle.initialized = true;
        }

        // Get leaf to insert into and set up to iterate through
        unsigned pageNumToInsertTo = getLeafPageByKey(ixFileHandle, attribute, key);

        char pageData[PAGE_SIZE];
        ixFileHandle.PFHandle->readPage(pageNumToInsertTo, pageData);
        ixFileHandle.PFHandle->readPageCounter -= 1;
        unsigned numEntries = getNumEntries(pageData);

        // Iterate through looking for a matching key (if one exists) and where to insert RID
        // Also calculate how much space
        bool keyInFirstHalf = true;
        bool keyAlreadyExists = false;
        unsigned halfwayPoint = 0;
        char* pageDataPtr = pageData + PAGE_METADATA;

        // Space already taken by metadata
        int spaceTaken = sizeof(unsigned) * 4;
        unsigned spotToInsert = PAGE_METADATA;
        for (unsigned i = 0; i < numEntries; i++) {
            bool moveSpotToInsert = false;
            if (attribute.type == TypeInt) {
                int currentKey;
                memcpy(&currentKey, pageDataPtr, sizeof(int));
                if (*(int *)key == currentKey) {
                    keyAlreadyExists = true;
                } else if (*(int *)key > currentKey) {
                    spotToInsert += sizeof(int);
                    moveSpotToInsert = true;
                }

                // If we are at the first entry in the second half, set halfwayPoint
                if (i == numEntries / 2) {
                    halfwayPoint = pageDataPtr - pageData;
                    if (*(int *)key >= currentKey) {
                        keyInFirstHalf = false;
                    }
                }

                pageDataPtr += sizeof(int);
                spaceTaken += sizeof(int);
            }
            else if (attribute.type == TypeReal) {
                float currentKey;
                memcpy(&currentKey, pageDataPtr, sizeof(float));

                if (*(float *)key == currentKey) {
                    keyAlreadyExists = true;
                } else if (*(float *)key > currentKey) {
                    spotToInsert += sizeof(float);
                    moveSpotToInsert = true;
                }

                // If we are at the first entry in the second half, set halfwayPoint
                if (i == (numEntries) / 2) {
                    halfwayPoint = spaceTaken - sizeof(unsigned);
                    if (*(float *)key >= currentKey) {
                        keyInFirstHalf = false;
                    }
                }

                pageDataPtr += sizeof(float);
                spaceTaken += sizeof(float);
            }
            else if (attribute.type == TypeVarChar) {
                unsigned entryStrLen;
                memcpy(&entryStrLen, pageDataPtr, sizeof(unsigned));
                std::string entryStr(pageDataPtr + sizeof(unsigned), entryStrLen);
                unsigned keyStrLen;
                memcpy(&keyStrLen, key, sizeof(unsigned));
                const char *keyCharData = reinterpret_cast<const char *>(key) + sizeof(unsigned);
                std::string keyStr(keyCharData, keyStrLen);

                if (keyStr == entryStr) {
                    keyAlreadyExists = true;
                } else if (keyStr > entryStr) {
                    spotToInsert += sizeof(unsigned) + entryStrLen;
                    moveSpotToInsert = true;
                }

                // If we are at the first entry in the second half, set halfwayPoint
                if (i == (numEntries) / 2) {
                    halfwayPoint = spaceTaken - sizeof(unsigned);
                    if (keyStr >= entryStr) {
                        keyInFirstHalf = false;
                    }
                }

                pageDataPtr += sizeof(unsigned) + entryStrLen;
                spaceTaken += sizeof(unsigned) + entryStrLen;
            }

            // Iterate past RID counter and however many RIDs
            unsigned numRIDsForEntry;
            memcpy(&numRIDsForEntry, pageDataPtr, sizeof(unsigned));

            if (moveSpotToInsert) {
                spotToInsert += numRIDsForEntry * (sizeof(unsigned) * 2) + sizeof(unsigned);
            }
            pageDataPtr += numRIDsForEntry * (sizeof(unsigned) * 2) + sizeof(unsigned);
            spaceTaken += numRIDsForEntry * (sizeof(unsigned) * 2) + sizeof(unsigned);
        }

        // Calculate if there is enough space
        bool enoughSpace = false;
        if (keyAlreadyExists) {
            if (PAGE_SIZE - spaceTaken - 2 * 4 >= 0) {
                enoughSpace = true;
            } else {
                enoughSpace = false;
            }
        } else {
            if (attribute.type == TypeInt || attribute.type == TypeReal) {
                if (PAGE_SIZE - spaceTaken - 4 * 4 >= 0) {
                    enoughSpace = true;
                } else {
                    enoughSpace = false;
                }
            } else if (attribute.type == TypeVarChar) {
                int keyStrLen;
                memcpy(&keyStrLen, key, sizeof(unsigned));
                if (PAGE_SIZE - spaceTaken - 4 - keyStrLen - 3 * 4 >= 0) {
                    enoughSpace = true;
                } else {
                    enoughSpace = false;
                }
            }
        }

        if (enoughSpace) {
            insertEntryIntoLeaf(pageData, spotToInsert, attribute, key, rid, keyAlreadyExists, spaceTaken);
            ixFileHandle.PFHandle->writePage(pageNumToInsertTo, pageData);
            return 0;
        } else {
            // Modify original page, pass on sibling from original to right
            unsigned newOriginalNumEntries = numEntries / 2;
            memcpy(pageData + sizeof(unsigned), &newOriginalNumEntries, sizeof(unsigned));
            unsigned currentSiblingPage;
            memcpy(&currentSiblingPage, pageData + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned));
            unsigned newSiblingPage = ixFileHandle.PFHandle->getNumberOfPages();
            memcpy(pageData + PAGE_SIZE - sizeof(unsigned), &newSiblingPage, sizeof(unsigned));
            // Create new page
            char newPageData[PAGE_SIZE];
            char* newPageDataPtr = newPageData;
            unsigned isLeafBit = 1;
            unsigned newPageNumEntries = ceil((double)numEntries / 2);
            unsigned parentPageNum = getParentPageNum(pageData);
            memcpy(newPageDataPtr, &isLeafBit, sizeof(unsigned));
            memcpy(newPageDataPtr + sizeof(unsigned), &newPageNumEntries, sizeof(unsigned));
            memcpy(newPageDataPtr + sizeof(unsigned) * 2, &parentPageNum, sizeof(unsigned));
            memcpy(newPageDataPtr + PAGE_METADATA, pageData + halfwayPoint, (spaceTaken - 4) - halfwayPoint);
            memcpy(newPageDataPtr + PAGE_SIZE - sizeof(unsigned), &currentSiblingPage, sizeof(unsigned));
            // Insert data into which ever page it needs to be in
            if (keyInFirstHalf) {
                insertEntryIntoLeaf(pageData, spotToInsert, attribute, key, rid, keyAlreadyExists, (halfwayPoint + 4));
            } else {
                spotToInsert = spotToInsert - halfwayPoint + sizeof(unsigned) * 3;
                spaceTaken = (spaceTaken) - halfwayPoint + sizeof(unsigned) * 3;
                insertEntryIntoLeaf(newPageData, spotToInsert, attribute, key, rid, keyAlreadyExists, spaceTaken);
            }
            ixFileHandle.PFHandle->writePage(pageNumToInsertTo, pageData);
            ixFileHandle.PFHandle->appendPage(newPageData);

            if (attribute.type == TypeInt || attribute.type == TypeReal) {
                unsigned keyToPushUp;
                memcpy(&keyToPushUp, newPageData + PAGE_METADATA, sizeof(unsigned));
                updateInternal(ixFileHandle, attribute, &keyToPushUp, pageNumToInsertTo, newSiblingPage, parentPageNum);
            } else if (attribute.type == TypeVarChar) {
                unsigned keyStrLen;
                memcpy(&keyStrLen, newPageData + PAGE_METADATA, sizeof(unsigned));
                std::string entryStr(newPageData + PAGE_METADATA + sizeof(unsigned), keyStrLen);
                // Allocate properly
                char *keyToPushUp = (char *)malloc(sizeof(unsigned) + keyStrLen);
                if (!keyToPushUp) {
                    std::cerr << "ERROR: Failed to allocate memory for keyToPushUp" << std::endl;
                    return -1;
                }
                // Copy key data safely
                memcpy(keyToPushUp, &keyStrLen, sizeof(unsigned));
                memcpy(keyToPushUp + sizeof(unsigned), newPageData + PAGE_METADATA + sizeof(unsigned), keyStrLen);
                updateInternal(ixFileHandle, attribute, keyToPushUp, pageNumToInsertTo, newSiblingPage, parentPageNum);
                free(keyToPushUp);
            }
            return 0;
        }
    }

    RC IndexManager::updateInternal(IXFileHandle &ixFileHandle, const Attribute &attribute,
                                const void *key, unsigned leftPageNum,
                                unsigned rightPageNum, unsigned parentPageNum) {
        // Handle new root
        if (parentPageNum == 0) {
            // Create new root page
            char newRootData[PAGE_SIZE];
            unsigned isLeafBit = 0;
            unsigned numEntries = 1;
            unsigned newParentPageNum = 0;
            memcpy(newRootData, &isLeafBit, sizeof(unsigned));
            memcpy(newRootData + sizeof(unsigned), &numEntries, sizeof(unsigned));
            memcpy(newRootData + sizeof(unsigned) * 2, &newParentPageNum, sizeof(unsigned));
            memcpy(newRootData + sizeof(unsigned) * 3, &leftPageNum, sizeof(unsigned));
            if (attribute.type == TypeInt || attribute.type == TypeReal) {
                memcpy(newRootData + sizeof(unsigned) * 4, key, sizeof(unsigned));
                memcpy(newRootData + sizeof(unsigned) * 5, &rightPageNum, sizeof(unsigned));
            } else if (attribute.type == TypeVarChar) {
                unsigned keyStrLen;
                memcpy(&keyStrLen, key, sizeof(unsigned));
                std::string keyStr(reinterpret_cast<const char*>(key) + sizeof(unsigned), keyStrLen);
                memcpy(newRootData + sizeof(unsigned) * 4, key, sizeof(unsigned) + keyStrLen);
                memcpy(newRootData + sizeof(unsigned) * 4 + sizeof(unsigned) + keyStrLen, &rightPageNum, sizeof(unsigned));
            }

            unsigned currentNumPages = ixFileHandle.PFHandle->getNumberOfPages();
            ixFileHandle.PFHandle->appendPage(newRootData);
            setRootPageNum(ixFileHandle, currentNumPages);

            // Update parent pointers in children
            char pageData[PAGE_SIZE];
            ixFileHandle.PFHandle->readPage(leftPageNum, pageData);
            memcpy(pageData + sizeof(unsigned) * 2, &currentNumPages, sizeof(unsigned));
            ixFileHandle.PFHandle->writePage(leftPageNum, pageData);

            ixFileHandle.PFHandle->readPage(rightPageNum, pageData);
            memcpy(pageData + sizeof(unsigned) * 2, &currentNumPages, sizeof(unsigned));
            ixFileHandle.PFHandle->writePage(rightPageNum, pageData);

            return 0;
        }

        // Read parent page
        char parentPageData[PAGE_SIZE];
        ixFileHandle.PFHandle->readPage(parentPageNum, parentPageData);
        unsigned parentNumEntries = getNumEntries(parentPageData);
        bool hasEnoughSpace = false;

        if (attribute.type == TypeInt || attribute.type == TypeReal) {
            hasEnoughSpace = calculateInternalSpaceTaken(parentPageData, attribute) + sizeof(unsigned) * 2 <= PAGE_SIZE;
        } else if (attribute.type == TypeVarChar) {
            unsigned keyStrLen;
            memcpy(&keyStrLen, key, sizeof(unsigned));
            hasEnoughSpace = calculateInternalSpaceTaken(parentPageData, attribute) + sizeof(unsigned) * 2 + keyStrLen <= PAGE_SIZE;
        }

        // If enough space, insert into the internal node and return
        if (hasEnoughSpace) {
            insertEntryIntoInternal(parentPageData, attribute, key, leftPageNum, rightPageNum);
            ixFileHandle.PFHandle->writePage(parentPageNum, parentPageData);
            return 0;
        }

        // Split the internal node
        unsigned halfwayPoint = 0;
        char* pageDataPtr = parentPageData + PAGE_METADATA + sizeof(unsigned);
        unsigned middleKeySize;
        char middleKeyBuffer[PAGE_SIZE];

        // Find the middle key
        for (unsigned i = 0; i < parentNumEntries; i++) {
            if (i == parentNumEntries / 2) {
                halfwayPoint = pageDataPtr - parentPageData;

                if (attribute.type == TypeInt || attribute.type == TypeReal) {
                    middleKeySize = sizeof(unsigned);
                    memcpy(middleKeyBuffer, pageDataPtr, middleKeySize);
                } else if (attribute.type == TypeVarChar) {
                    unsigned entryStrLen;
                    memcpy(&entryStrLen, pageDataPtr, sizeof(unsigned));
                    middleKeySize = sizeof(unsigned) + entryStrLen;
                    memcpy(middleKeyBuffer, pageDataPtr, middleKeySize);
                }
            }
            if (attribute.type == TypeInt || attribute.type == TypeReal) {
                pageDataPtr += sizeof(unsigned) * 2;  // Skip key + child pointer
            } else if (attribute.type == TypeVarChar) {
                unsigned entryStrLen;
                memcpy(&entryStrLen, pageDataPtr, sizeof(unsigned));
                pageDataPtr += sizeof(unsigned) * 2 + entryStrLen;
            }
        }

        /*
        // Find the middle key
        for (unsigned i = 0; i < parentNumEntries; i++) {
            if (i == parentNumEntries / 2) {
                halfwayPoint = pageDataPtr - parentPageData;
                middleKey = pageDataPtr;

                if (attribute.type == TypeInt || attribute.type == TypeReal) {
                    middleKeySize = sizeof(unsigned);
                    memcpy(middleKeyBuffer, pageDataPtr, middleKeySize);
                } else if (attribute.type == TypeVarChar) {
                    unsigned entryStrLen;
                    memcpy(&entryStrLen, pageDataPtr, sizeof(unsigned));
                    middleKeySize = sizeof(unsigned) + entryStrLen;
                    memcpy(middleKeyBuffer, pageDataPtr, middleKeySize);
                }
            }
            if (attribute.type == TypeInt || attribute.type == TypeReal) {
                pageDataPtr += sizeof(unsigned) * 2; // Skip key + child pointer
            } else if (attribute.type == TypeVarChar) {
                unsigned entryStrLen;
                memcpy(&entryStrLen, pageDataPtr, sizeof(unsigned));
                pageDataPtr += sizeof(unsigned) * 2 + entryStrLen;
            }
        } */

        // Create new internal node
        char newInternalPageData[PAGE_SIZE];
        unsigned isLeafBit = 0;
        unsigned numEntries = (parentNumEntries - 1) / 2;
        unsigned newParentPageNum = getParentPageNum(parentPageData);

        memcpy(newInternalPageData, &isLeafBit, sizeof(unsigned));
        memcpy(newInternalPageData + sizeof(unsigned), &numEntries, sizeof(unsigned));
        memcpy(newInternalPageData + sizeof(unsigned) * 2, &newParentPageNum, sizeof(unsigned));
        memcpy(newInternalPageData + sizeof(unsigned) * 3, parentPageData + halfwayPoint + middleKeySize,
               (pageDataPtr - parentPageData) - (halfwayPoint + middleKeySize));

        // Update original page and insert key into the correct split node
        unsigned newOriginalNumEntries = parentNumEntries / 2;
        memcpy(parentPageData + sizeof(unsigned), &newOriginalNumEntries, sizeof(unsigned));

        unsigned newPageNum = ixFileHandle.PFHandle->getNumberOfPages();

        // Update parent pointers for children moving to the right node
        char childPageData[PAGE_SIZE];
        char *newPageDataPtr = newInternalPageData + PAGE_METADATA;
        for (unsigned i = 0; i <= numEntries; i++) {  // Includes the last rightmost pointer
            unsigned childPageNum;
            memcpy(&childPageNum, newPageDataPtr, sizeof(unsigned));

            // Read the child page
            ixFileHandle.PFHandle->readPage(childPageNum, childPageData);
            // Update its parent pointer
            memcpy(childPageData + sizeof(unsigned) * 2, &newPageNum, sizeof(unsigned));
            // Write back the updated child page
            ixFileHandle.PFHandle->writePage(childPageNum, childPageData);

            // Move to the next child pointer
            if (attribute.type == TypeInt || attribute.type == TypeReal) {
                newPageDataPtr += sizeof(unsigned) * 2; // Skip key + child pointer
            } else if (attribute.type == TypeVarChar) {
                unsigned keyStrLen;
                memcpy(&keyStrLen, newPageDataPtr + sizeof(unsigned), sizeof(unsigned));
                newPageDataPtr += sizeof(unsigned) * 2 + keyStrLen;
            }
        }

        // Prepare key for recursive call
        void* middleKey;
        if (attribute.type == TypeVarChar) {
            middleKey = malloc(middleKeySize);
            memcpy(middleKey, middleKeyBuffer, middleKeySize);
        } else {
            middleKey = middleKeyBuffer; // Ints and Floats use buffer directly
        }

        // Insert key in correct split node
        bool insertLeft = false;
        if (attribute.type == TypeInt) {
            insertLeft = (*(int*)key < *(int*)middleKey);
        } else if (attribute.type == TypeReal) {
            insertLeft = (*(float*)key < *(float*)middleKey);
        } else if (attribute.type == TypeVarChar) {
            unsigned keyLen, middleKeyLen;
            memcpy(&keyLen, key, sizeof(unsigned));
            memcpy(&middleKeyLen, middleKeyBuffer, sizeof(unsigned));

            std::string keyStr(reinterpret_cast<const char*>(key) + sizeof(unsigned), keyLen);
            std::string middleKeyStr(reinterpret_cast<const char*>(middleKeyBuffer) + sizeof(unsigned), middleKeyLen);

            insertLeft = keyStr < middleKeyStr;
        }

        /*
        bool insertLeft;
        if (attribute.type == TypeInt) {
            if (*(int*)key < *(int*)middleKey) {
                insertLeft = true;
            } else {
                insertLeft = false;
            }
        } else if (attribute.type == TypeReal) {
            if (*(float*)key < *(float*)middleKey) {
                insertLeft = true;
            } else {
                insertLeft = false;
            }
        } else if (attribute.type == TypeVarChar) {
            unsigned keyLen, middleKeyLen;
            memcpy(&keyLen, key, sizeof(unsigned));
            memcpy(&middleKeyLen, middleKeyBuffer, sizeof(unsigned));

            std::string keyStr(reinterpret_cast<const char*>(key) + sizeof(unsigned), keyLen);
            std::string middleKeyStr(reinterpret_cast<const char*>(middleKeyBuffer) + sizeof(unsigned), middleKeyLen);

            insertLeft = keyStr < middleKeyStr;
        } */

        if (insertLeft) {
            insertEntryIntoInternal(parentPageData, attribute, key, leftPageNum, rightPageNum);
        } else {
            insertEntryIntoInternal(newInternalPageData, attribute, key, leftPageNum, rightPageNum);
            char pageData[PAGE_SIZE];
            ixFileHandle.PFHandle->readPage(leftPageNum, pageData);
            memcpy(pageData + sizeof(unsigned) * 2, &newPageNum, sizeof(unsigned));
            ixFileHandle.PFHandle->writePage(leftPageNum, pageData);

            ixFileHandle.PFHandle->readPage(rightPageNum, pageData);
            memcpy(pageData + sizeof(unsigned) * 2, &newPageNum, sizeof(unsigned));
            ixFileHandle.PFHandle->writePage(rightPageNum, pageData);
        }

        // Write updated pages
        ixFileHandle.PFHandle->writePage(parentPageNum, parentPageData);
        ixFileHandle.PFHandle->appendPage(newInternalPageData);

        // Recursive call to push up the middle key
        updateInternal(ixFileHandle, attribute, middleKey, parentPageNum, newPageNum, newParentPageNum);
        if (attribute.type == TypeVarChar) free(middleKey);
        return 0;
    }

    RC IndexManager::deleteEntry(IXFileHandle &ixFileHandle, const Attribute &attribute, const void *key, const RID &rid) {
        // Load the page containing the entry to delete
        unsigned targetPageNum = getLeafPageByKey(ixFileHandle, attribute, key);

        char pageData[PAGE_SIZE];
        ixFileHandle.PFHandle->readPage(targetPageNum, pageData);

        // Read metadata and set up iteration
        unsigned numEntries = getNumEntries(pageData);

        char *pageDataPtr = pageData + PAGE_METADATA;
        unsigned keyOffset = 0;
        unsigned keySize = 0;
        bool keyFound = false;
        unsigned numRIDs = 0;
        char *ridListPtr = nullptr;

        // Search for key
        for (unsigned i = 0; i < numEntries; i++) {
            keyOffset = pageDataPtr - pageData; // Track key position

            if (attribute.type == TypeInt) {
                int currentKey;
                memcpy(&currentKey, pageDataPtr, sizeof(int));
                if (*(int *)key == currentKey) {
                    keyFound = true;
                    keySize = sizeof(int);
                }
                pageDataPtr += sizeof(int);
            } else if (attribute.type == TypeReal) {
                float currentKey;
                memcpy(&currentKey, pageDataPtr, sizeof(float));
                if (*(float *)key == currentKey) {
                    keyFound = true;
                    keySize = sizeof(float);
                }
                pageDataPtr += sizeof(float);
            } else if (attribute.type == TypeVarChar) {
                unsigned keyStrLen;
                memcpy(&keyStrLen, pageDataPtr, sizeof(unsigned));
                std::string entryStr(pageDataPtr + sizeof(unsigned), keyStrLen);
                unsigned givenStrLen;
                memcpy(&givenStrLen, key, sizeof(unsigned));
                std::string givenStr(reinterpret_cast<const char *>(key) + sizeof(unsigned), givenStrLen);
                if (entryStr == givenStr) {
                    keyFound = true;
                    keySize = sizeof(unsigned) + keyStrLen;
                }
                pageDataPtr += sizeof(unsigned) + keyStrLen;
            }

            memcpy(&numRIDs, pageDataPtr, sizeof(unsigned));
            ridListPtr = pageDataPtr + sizeof(unsigned); // Set pointer to first RID

            if (keyFound) {
                break; // Stop searching if we found the key
            }

            // Skip over this key's RIDs and move to the next key
            pageDataPtr += sizeof(unsigned) + numRIDs * (sizeof(unsigned) * 2);
        }

        if (!keyFound) {
            return -1;
        }

        // Search for RID
        bool ridFound = false;
        char *targetRIDPtr = nullptr;

        for (unsigned j = 0; j < numRIDs; j++) {
            RID currentRID;
            memcpy(&currentRID.pageNum, ridListPtr, sizeof(unsigned));
            memcpy(&currentRID.slotNum, ridListPtr + sizeof(unsigned), sizeof(unsigned));
            if (currentRID.pageNum == rid.pageNum && currentRID.slotNum == rid.slotNum) {
                ridFound = true;
                targetRIDPtr = ridListPtr;
                break;
            }
            ridListPtr += sizeof(unsigned) * 2;
        }

        if (!ridFound) {
            return -1;
        }

        // If this is the only RID for the key, remove the key entirely
        if (numRIDs == 1) {
            unsigned entrySize = keySize + sizeof(unsigned) + numRIDs * (sizeof(unsigned) * 2);  // Key + RID count + all RIDs
            unsigned bytesToMove = (pageData + PAGE_SIZE) - (pageDataPtr + entrySize);
            memmove(pageData + keyOffset, pageData + keyOffset + entrySize, bytesToMove);

            // Update number of entries
            unsigned updatedNumEntries = numEntries - 1;
            memcpy(pageData + sizeof(unsigned), &updatedNumEntries, sizeof(unsigned));
        } else {
            // Shift RIDs left
            unsigned bytesToMove = (pageData + PAGE_SIZE) - (targetRIDPtr + sizeof(unsigned) * 2);
            memmove(targetRIDPtr, targetRIDPtr + sizeof(unsigned) * 2, bytesToMove);

            // Update RID count
            unsigned updatedNumRIDs = numRIDs - 1;
            memcpy(pageDataPtr, &updatedNumRIDs, sizeof(unsigned));
        }

        // Write modified page back
        ixFileHandle.PFHandle->writePage(targetPageNum, pageData);

        return 0;
    }

    bool fileExists(const std::string &fileName) {
        std::ifstream file(fileName);
        return file.good();
    }

    // Helper function to copy file
    RC IndexManager::copyFile(const std::string &sourceFile, const std::string &destFile) {
        IXFileHandle srcHandle, destHandle;
        if (openFile(sourceFile, srcHandle) == -1 || openFile(destFile, destHandle) == -1) {
            return -1;
        }

        unsigned numPages = srcHandle.PFHandle->getNumberOfPages();
        char pageData[PAGE_SIZE];

        for (unsigned i = 0; i < numPages; i++) {
            srcHandle.PFHandle->readPage(i, pageData);
            destHandle.PFHandle->appendPage(pageData);
        }

        closeFile(srcHandle);
        closeFile(destHandle);
        return 0;
    }

    RC IndexManager::scan(IXFileHandle &ixFileHandle,
                          const Attribute &attribute,
                          const void *lowKey,
                          const void *highKey,
                          bool lowKeyInclusive,
                          bool highKeyInclusive,
                          IX_ScanIterator &ix_ScanIterator) {
        // Check that fileHandle is legit
        if (!fileExists(ixFileHandle.PFHandle->fileName)) {
            std::cerr << "File " << ixFileHandle.PFHandle->fileName << " not found." << std::endl;
            return -1;
        }

        // Initialize scanner
        IndexManager &im = IndexManager::instance();
        std::string scanIteratorFileName = ixFileHandle.PFHandle->fileName + "_scan";
        IXFileHandle *newIXFileHandle = new IXFileHandle();
        im.createFile(scanIteratorFileName);
        /*
        if (im.createFile(scanIteratorFileName) == -1) {
            std::cerr << "File " << scanIteratorFileName << " creation failed." << std::endl;
            return -1;
        } */
        if (im.openFile(scanIteratorFileName, *newIXFileHandle) == -1) {
            std::cerr << "File " << scanIteratorFileName << " open failed." << std::endl;
            return -1;
        }
        ix_ScanIterator.initialize(scanIteratorFileName, attribute, *newIXFileHandle);
        // Don't waste time building a replica
        if (lowKey == nullptr && highKey == nullptr) {
            im.copyFile(ixFileHandle.PFHandle->fileName, scanIteratorFileName);
            ixFileHandle.PFHandle->readPageCounter += 2;
            return 0;
        }

        // Locate the starting leaf page based on lowKey
        unsigned currentPageNum;
        if (lowKey == nullptr) {
            // Start from the root and navigate left until we reach a leaf node
            currentPageNum = getRootPageNum(ixFileHandle);
            char pageData[PAGE_SIZE];
            while (true) {
                ixFileHandle.PFHandle->readPage(currentPageNum, pageData);
                if (isLeafNode(pageData)) {
                    break;  // We've reached a leaf node
                }
                // Move to the first child (leftmost)
                memcpy(&currentPageNum, pageData + PAGE_METADATA, sizeof(unsigned));
            }
        } else {
            // Otherwise, find the leaf node where lowKey resides
            currentPageNum = getLeafPageByKey(ixFileHandle, attribute, lowKey);
        }

        // Traverse leaf pages
        char pageData[PAGE_SIZE];
        while (currentPageNum != 0) {  // Page 0 means no more pages to process
            ixFileHandle.PFHandle->readPage(currentPageNum, pageData);
            unsigned numEntries = getNumEntries(pageData);
            char *pageDataPtr = pageData + PAGE_METADATA;
            // Iterate over keys in the leaf node
            bool pastHighKey = false;
            for (unsigned i = 0; i < numEntries; i++) {
                bool withinRange = false;
                unsigned keySize = 0;

                // Determine key and key size
                if (attribute.type == TypeInt) {
                    int currentKey;
                    memcpy(&currentKey, pageDataPtr, sizeof(int));
                    keySize = sizeof(int);
                    int ridCounter;
                    memcpy(&ridCounter, pageDataPtr + sizeof(int), sizeof(int));

                    if ((lowKey == nullptr || (lowKeyInclusive ? currentKey >= *(int*)lowKey : currentKey > *(int*)lowKey)) &&
                        (highKey == nullptr || (highKeyInclusive ? currentKey <= *(int*)highKey : currentKey < *(int*)highKey))) {
                        withinRange = true;
                    } else {
                        if (highKey != nullptr) {
                            if (highKeyInclusive ? currentKey > *(int*)highKey : currentKey >= *(int*)highKey) {
                                pastHighKey = true;
                                break;
                            }
                        }
                    }
                } else if (attribute.type == TypeReal) {
                    float currentKey;
                    memcpy(&currentKey, pageDataPtr, sizeof(float));
                    keySize = sizeof(float);
                    if ((lowKey == nullptr || (lowKeyInclusive ? currentKey >= *(float*)lowKey : currentKey > *(float*)lowKey)) &&
                        (highKey == nullptr || (highKeyInclusive ? currentKey <= *(float*)highKey : currentKey < *(float*)highKey))) {
                        withinRange = true;
                        } else {
                            if (highKey != nullptr) {
                                if (highKeyInclusive ? currentKey > *(float*)highKey : currentKey >= *(float*)highKey) {
                                    pastHighKey = true;
                                    break;
                                }
                            }
                        }
                } else if (attribute.type == TypeVarChar) {
                    unsigned strLen;
                    memcpy(&strLen, pageDataPtr, sizeof(unsigned));
                    std::string currentKey(pageDataPtr + sizeof(unsigned), strLen);
                    keySize = sizeof(unsigned) + strLen;

                    std::string lowStr, highStr;
                    if (lowKey) {
                        unsigned lowStrLen;
                        memcpy(&lowStrLen, lowKey, sizeof(unsigned));
                        lowStr = std::string(reinterpret_cast<const char*>(lowKey) + sizeof(unsigned), lowStrLen);
                    }
                    if (highKey) {
                        unsigned highStrLen;
                        memcpy(&highStrLen, highKey, sizeof(unsigned));
                        highStr = std::string(reinterpret_cast<const char*>(highKey) + sizeof(unsigned), highStrLen);
                    }

                    if ((lowKey == nullptr || (lowKeyInclusive ? currentKey >= lowStr : currentKey > lowStr)) &&
                        (highKey == nullptr || (highKeyInclusive ? currentKey <= highStr : currentKey < highStr))) {
                        withinRange = true;
                        } else {
                            if (highKey != nullptr) {
                                if (highKeyInclusive ? currentKey > highStr : currentKey >= highStr) {
                                    pastHighKey = true;
                                    break;
                                }
                            }
                        }
                }
                void *keyCopy = malloc(keySize);
                memcpy(keyCopy, pageDataPtr, keySize);
                // Move pointer past key
                pageDataPtr += keySize;

                // Read number of RIDs
                unsigned numRIDs;
                memcpy(&numRIDs, pageDataPtr, sizeof(unsigned));
                char *ridListPtr = pageDataPtr + sizeof(unsigned);

                if (withinRange) {
                    // Insert valid entries into iterator
                    for (unsigned j = 0; j < numRIDs; j++) {
                        RID rid;
                        memcpy(&rid.pageNum, ridListPtr, sizeof(unsigned));
                        memcpy(&rid.slotNum, ridListPtr + sizeof(unsigned), sizeof(unsigned));
                        //ix_ScanIterator.insertScanEntry(pageDataPtr - keySize, rid);
                        ix_ScanIterator.insertScanEntry(keyCopy, rid);
                        ridListPtr += sizeof(unsigned) * 2;
                    }
                }
                free(keyCopy);
                // Move to next key
                pageDataPtr += sizeof(unsigned) + numRIDs * (sizeof(unsigned) * 2);
            }
            // Move to next sibling page or return
            if (pastHighKey) {
                return 0;
            } else {
                memcpy(&currentPageNum, pageData + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned));
            }
        }
        return 0;
    }

    RC IndexManager::printBTree(IXFileHandle &ixFileHandle, const Attribute &attribute, std::ostream &out) const {
        char zeroPage[PAGE_SIZE] = {0};
        if (ixFileHandle.PFHandle->readPage(0, zeroPage) == -1) {
            std::cerr << "Error reading root page" << std::endl;
            return -1;
        }
        unsigned rootPageNum;
        memcpy(&rootPageNum, zeroPage, sizeof(unsigned));
        if (rootPageNum == 0) {
            out << "{}"; // Empty tree
            return 0;
        }
        printHelper(ixFileHandle, attribute, out, rootPageNum, 0);
        // printHelper(ixFileHandle, attribute, std::cout, rootPageNum, 0);
        return 0;
    }

    /*
    {"keys":["P"],
    "children":[
    {"keys":["C","G","M"],
    "children": [
    {"keys": ["A:[(1,1),(1,2)]","B:[(2,1),(2,2)]"]},
    {"keys": ["D:[(3,1),(3,2)]","E:[(4,1)]","F:[(5,1)]"]},
    {"keys": ["J:[(5,1),(5,2)]","K:[(6,1),(6,2)]","L:[(7,1)]"]},
    {"keys": ["N:[(8,1)]","O:[(9,1)]"]}
    ]},
    {"keys":["T","X"],
    "children": [
    {"keys": ["Q:[(10,1)]","R:[(11,1)]","S:[(12,1)]"]},
    {"keys": ["U:[(13,1)]","V:[(14,1)]"]},
    {"keys": ["Y:[(15,1)]","Z:[(16,1)]"]}
    ]}
    ]} */
    void IndexManager::printHelper(IXFileHandle &ixFileHandle, const Attribute &attribute, std::ostream &out, const unsigned pageNum, const unsigned level) const {
        char pageData[PAGE_SIZE];
        ixFileHandle.PFHandle->readPage(pageNum, pageData);

        // Read metadata
        unsigned isLeafBit;
        unsigned numEntries;
        unsigned parentPageNum;
        memcpy(&isLeafBit, pageData, sizeof(unsigned));
        memcpy(&numEntries, pageData + sizeof(unsigned), sizeof(unsigned));
        memcpy(&parentPageNum, pageData + sizeof(unsigned) * 2, sizeof(unsigned));

        // Indent (needed?)
        std::string indent(level * 4, ' ');

        if (isLeafBit) {
            // Leaf node
            out << indent << "{\"keys\":[";
            char *pageDataPtr = pageData + PAGE_METADATA;
            for (unsigned i = 0; i < numEntries; ++i) {
                if (i > 0) {
                    out << "," << std::endl;
                }
                if (attribute.type == TypeInt) {
                    int key;
                    memcpy(&key, pageDataPtr, sizeof(int));
                    pageDataPtr += sizeof(int);
                    unsigned numRIDs;
                    memcpy(&numRIDs, pageDataPtr, sizeof(unsigned));
                    pageDataPtr += sizeof(unsigned);
                    out << "\"" << key << ":[";
                    for (unsigned j = 0; j < numRIDs; ++j) {
                        if (j > 0) {
                            out << ",";
                        }
                        unsigned pageNum;
                        unsigned slotNum;
                        memcpy(&pageNum, pageDataPtr, sizeof(unsigned));
                        pageDataPtr += sizeof(unsigned);
                        memcpy(&slotNum, pageDataPtr, sizeof(unsigned));
                        out << "(" << pageNum << "," << slotNum << ")";
                        pageDataPtr += sizeof(unsigned);
                    }
                    out << "]\"";
                } else if (attribute.type == TypeReal) {
                    float key;
                    memcpy(&key, pageDataPtr, sizeof(int));
                    pageDataPtr += sizeof(int);
                    unsigned numRIDs;
                    memcpy(&numRIDs, pageDataPtr, sizeof(unsigned));
                    pageDataPtr += sizeof(unsigned);
                    out << "\"" << key << ":[";
                    for (unsigned j = 0; j < numRIDs; ++j) {
                        if (j > 0) {
                            out << ",";
                        }
                        unsigned pageNum;
                        unsigned slotNum;
                        memcpy(&pageNum, pageDataPtr, sizeof(unsigned));
                        pageDataPtr += sizeof(unsigned);
                        memcpy(&slotNum, pageDataPtr, sizeof(unsigned));
                        out << "(" << pageNum << "," << slotNum << ")";
                        pageDataPtr += sizeof(unsigned);
                    }
                    out << "]\"";
                } else if (attribute.type == TypeVarChar) {
                    unsigned keyStrLen;
                    memcpy(&keyStrLen, pageDataPtr, sizeof(unsigned));
                    pageDataPtr += sizeof(unsigned);
                    std::string key(pageDataPtr, keyStrLen);
                    pageDataPtr += keyStrLen;
                    unsigned numRIDs;
                    memcpy(&numRIDs, pageDataPtr, sizeof(unsigned));
                    pageDataPtr += sizeof(unsigned);
                    out << "\"" << key << ":[";
                    for (unsigned j = 0; j < numRIDs; ++j) {
                        if (j > 0) {
                            out << ",";
                        }
                        unsigned pageNum;
                        unsigned slotNum;
                        memcpy(&pageNum, pageDataPtr, sizeof(unsigned));
                        pageDataPtr += sizeof(unsigned);
                        memcpy(&slotNum, pageDataPtr, sizeof(unsigned));
                        out << "(" << pageNum << "," << slotNum << ")";
                        pageDataPtr += sizeof(unsigned);
                    }
                    out << "]\"";
                }
            }
            out << "]}";
        } else {
            // Internal node
            out << indent << "{\"keys\":[";
            char *pageDataPtr = pageData + PAGE_METADATA + sizeof(unsigned);
            for (unsigned i = 0; i < numEntries; ++i) {
                if (attribute.type == TypeInt) {
                    int key;
                    memcpy(&key, pageDataPtr, sizeof(int));
                    out << "\"" << key << "\"";
                    pageDataPtr += sizeof(int) * 2;
                } else if (attribute.type == TypeReal) {
                    float key;
                    memcpy(&key, pageDataPtr, sizeof(float));
                    out << "\"" << key << "\"";
                    pageDataPtr += sizeof(float) * 2;
                } else if (attribute.type == TypeVarChar) {
                    unsigned keyStrLen;
                    memcpy(&keyStrLen, pageDataPtr, sizeof(unsigned));
                    pageDataPtr += sizeof(unsigned);
                    std::string key(pageDataPtr, keyStrLen);
                    out << "\"" << key << "\"";
                    pageDataPtr += keyStrLen + sizeof(int);
                }
                if (i < numEntries - 1) {
                    out << ",";
                }
            }
            out << "],\n";
            out << indent << "\"children\":[\n";
            pageDataPtr = pageData + PAGE_METADATA;
            for (unsigned i = 0; i <= numEntries; ++i) {
                unsigned childPageNum;
                memcpy(&childPageNum, pageDataPtr, sizeof(unsigned));
                unsigned moveSize = 0;
                if (attribute.type == TypeInt || attribute.type == TypeReal) {
                    moveSize = sizeof(unsigned);
                } else if (attribute.type == TypeVarChar) {
                    unsigned keyStrLen;
                    memcpy(&keyStrLen, pageDataPtr + sizeof(unsigned), sizeof(unsigned));
                    moveSize = sizeof(unsigned) + keyStrLen;
                }
                pageDataPtr += sizeof(unsigned) + moveSize;
                printHelper(ixFileHandle, attribute, out, childPageNum, level + 1);
                if (i < numEntries) {
                    out << ",\n";
                }
            }
            out << indent << "]}\n";
        }
    }

    /*
    // Helper function to print RIDs
    std::string IndexManager::printRIDs(const std::vector<RID>& rids) {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < rids.size(); ++i) {
            oss << "(" << rids[i].pageNum << "," << rids[i].slotNum << ")";
            if (i < rids.size() - 1) {
                oss << ",";
            }
        }
        oss << "]";
        return oss.str();
    } */

    IX_ScanIterator::IX_ScanIterator() {
    }

    IX_ScanIterator::~IX_ScanIterator() {
    }

    RC IX_ScanIterator::initialize(std::string fileName, const Attribute &attribute, IXFileHandle &ixFileHandle) {
        this->currentPage = 0;
        this->currentKeyIndex = 0;
        this->currentRIDIndex = 0;
        this->fileName = std::move(fileName);
        this->attribute = attribute;
        this->ixFileHandle = &ixFileHandle;
        IndexManager &im = IndexManager::instance();
        return 0;
    }

    RC IX_ScanIterator::insertScanEntry(const void *key, const RID &rid) {
        IndexManager &im = IndexManager::instance();
        im.insertEntry(*ixFileHandle, this->attribute, key, rid);
        return 0;
    }

    RC IX_ScanIterator::getNextEntry(RID &rid, void *key) {
        if (!this->ixFileHandle) return -1; // No file handle means no entries
        IndexManager &im = IndexManager::instance();
        char pageData[PAGE_SIZE];
        // If this is the first call, start at the leftmost leaf node
        if (this->currentPage == 0) {
            IndexManager &im = IndexManager::instance();
            this->currentPage = im.getRootPageNum(*this->ixFileHandle);
            // Traverse to the leftmost leaf node
            while (true) {
                this->ixFileHandle->PFHandle->readPage(this->currentPage, pageData);
                if (im.isLeafNode(pageData)) break;

                // Internal node: get the leftmost child
                unsigned leftmostChild;
                memcpy(&leftmostChild, pageData + PAGE_METADATA, sizeof(unsigned));
                this->currentPage = leftmostChild;
            }
        }

        this->ixFileHandle->PFHandle->readPage(this->currentPage, pageData);

        unsigned numEntries = im.getNumEntries(pageData);
        if (this->currentKeyIndex >= numEntries) { // If we exhausted the page, move to the next
            unsigned nextSiblingPage;
            memcpy(&nextSiblingPage, pageData + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned));
            if (nextSiblingPage == 0) return IX_EOF; // No more pages
            this->currentPage = nextSiblingPage;
            this->ixFileHandle->PFHandle->readPage(this->currentPage, pageData);
            this->currentKeyIndex = 0;
            this->currentRIDIndex = 0;
        }

        // Locate the key and its RID list
        char *pageDataPtr = pageData + PAGE_METADATA;
        for (unsigned i = 0; i < this->currentKeyIndex; i++) {
            if (this->attribute.type == TypeInt || this->attribute.type == TypeReal) {
                pageDataPtr += sizeof(int);
            } else if (this->attribute.type == TypeVarChar) {
                unsigned strLen;
                memcpy(&strLen, pageDataPtr, sizeof(unsigned));
                pageDataPtr += sizeof(unsigned) + strLen;
            }
            unsigned numRIDs;
            memcpy(&numRIDs, pageDataPtr, sizeof(unsigned));
            pageDataPtr += sizeof(unsigned) + numRIDs * (sizeof(unsigned) * 2);
        }

        // Extract the key
        if (this->attribute.type == TypeInt) {
            memcpy(key, pageDataPtr, sizeof(int));
            pageDataPtr += sizeof(int);
        } else if (this->attribute.type == TypeReal) {
            memcpy(key, pageDataPtr, sizeof(float));
            pageDataPtr += sizeof(float);
        } else if (this->attribute.type == TypeVarChar) {
            unsigned strLen;
            memcpy(&strLen, pageDataPtr, sizeof(unsigned));
            memcpy(key, pageDataPtr, sizeof(unsigned) + strLen);
            pageDataPtr += sizeof(unsigned) + strLen;
        }

        // Extract the RID
        unsigned numRIDs;
        memcpy(&numRIDs, pageDataPtr, sizeof(unsigned));
        pageDataPtr += sizeof(unsigned);
        //char *ridListPtr = pageDataPtr + ((numRIDs - 1) * 2 * sizeof(unsigned)) - (this->currentRIDIndex * (sizeof(unsigned) * 2));
        char *ridListPtr = pageDataPtr + (this->currentRIDIndex * (sizeof(unsigned) * 2));

        memcpy(&rid.pageNum, ridListPtr, sizeof(unsigned));
        memcpy(&rid.slotNum, ridListPtr + sizeof(unsigned), sizeof(unsigned));
        // Update iterator position
        this->currentRIDIndex++;
        if (this->currentRIDIndex >= numRIDs) {
            this->currentRIDIndex = 0;
            this->currentKeyIndex++;
        }

        return 0;
    }

    RC IX_ScanIterator::close() {
        if (this->ixFileHandle) {
            IndexManager &im = IndexManager::instance();
            im.closeFile(*this->ixFileHandle);
            im.destroyFile(this->fileName);
            delete ixFileHandle;
            ixFileHandle = nullptr;
            currentPage = 0;
            currentKeyIndex = 0;
            currentRIDIndex = 0;
            fileName.clear();
        }
        return 0;
    }

    IXFileHandle::IXFileHandle() {
        ixReadPageCounter = 0;
        ixWritePageCounter = 0;
        ixAppendPageCounter = 0;
    }

    IXFileHandle::~IXFileHandle() {
    }

    RC
    IXFileHandle::associateFileHandle(FileHandle &fileHandle) {
        this->PFHandle = &fileHandle;
        this->fileHandleSet = true;
        return 0;
    }

    RC
    IXFileHandle::collectCounterValues(unsigned &readPageCount, unsigned &writePageCount, unsigned &appendPageCount) {
        if (this->PFHandle->collectCounterValues(readPageCount, writePageCount, appendPageCount) == -1) {
            std::cerr << "collectCounterValues failed" << std::endl;
            return -1;
        }
        return 0;
    }

} // namespace PeterDB
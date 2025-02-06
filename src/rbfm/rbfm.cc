#include "../include/rbfm.h"
#include <cstring>
#include <cstdint>
#include <cmath>
#include <utility>
#include <string>
#include <cstdlib>
#include <vector>
#include <algorithm>

namespace PeterDB {
    RecordBasedFileManager &RecordBasedFileManager::instance() {
        static RecordBasedFileManager _rbf_manager = RecordBasedFileManager();
        return _rbf_manager;
    }

    RecordBasedFileManager::RecordBasedFileManager() = default;

    RecordBasedFileManager::~RecordBasedFileManager() = default;

    RecordBasedFileManager::RecordBasedFileManager(const RecordBasedFileManager &) = default;

    RecordBasedFileManager &RecordBasedFileManager::operator=(const RecordBasedFileManager &) = default;

    RC RecordBasedFileManager::createFile(const std::string &fileName) {
        PagedFileManager &pfm = PagedFileManager::instance();
        if (pfm.createFile(fileName) == -1) {
            return -1;
        }
        return 0;
    }

    RC RecordBasedFileManager::destroyFile(const std::string &fileName) {
        PagedFileManager &pfm = PagedFileManager::instance();
        pfm.destroyFile(fileName);
        return 0;
    }

    RC RecordBasedFileManager::openFile(const std::string &fileName, FileHandle &fileHandle) {
        PagedFileManager &pfm = PagedFileManager::instance();
        if (pfm.openFile(fileName, fileHandle) == -1) {
            return -1;
        }
        return 0;
    }

    RC RecordBasedFileManager::closeFile(FileHandle &fileHandle) {
        PagedFileManager &pfm = PagedFileManager::instance();
        pfm.closeFile(fileHandle);
        return 0;
    }

    // We will have a slot directory in the footer of each page which will grow from right to left
    // From right to left, the slot directory will contain # of slots used, pointer to free space, and slots
    // Slots are a tuple of offset (location on the page) and length, and are numbered from right to left
    // Records will contain a header with the null array and length of each field
    // We will have a slot directory in the footer of each page which will grow from right to left
    // From right to left, the slot directory will contain # of slots used, pointer to free space, and slots
    // Slots are a tuple of offset (location on the page) and length, and are numbered from right to left
    // Records will contain a header with the null array and length of each field
    RC RecordBasedFileManager::insertRecord(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor,
                                        const void *data, RID &rid) {
        // Check if there is more than just the metadata page available, if not, append a page with a slot directory
        if (fileHandle.getNumberOfPages() == 0) {
            appendRecordPage(fileHandle);
        }

        // Create the record using the record descriptor and data, complete with the null array and length of fields array, taking note of its total size
        size_t recordSize = 0;
        void *record = createRecord(recordDescriptor, data, recordSize);

        // Now that we have a page to work with, check the slot directory for where to insert the record
        // If there is no space in all the pages, we will need to again append a page with a slot directory
        unsigned pageNum = getInsertRecordPage(fileHandle, recordSize);
        // Once the page is located, insert the record referencing the slot directory pointer to free space
        // Then, update the slot directory by incrementing the number of slots used, and adding a slot tuple with the location and length of the record
        // Finally, fill in the RID parameter
        insertRecordIntoPage(fileHandle, pageNum, recordSize, record, rid);
        free(record);
        return 0;
    }

    // Helper functions for insert

    // Append a new page w/ the beginner slot directory (0 slots and free space at 0)
    void RecordBasedFileManager::appendRecordPage(FileHandle &fileHandle) {
        char newPage[PAGE_SIZE] = {0};
        unsigned numSlots = 0;
        unsigned freeSpacePointer = 0;
        memcpy(newPage + PAGE_SIZE - sizeof(numSlots), &numSlots, sizeof(numSlots));
        memcpy(newPage + PAGE_SIZE - sizeof(numSlots) - sizeof(freeSpacePointer), &freeSpacePointer, sizeof(freeSpacePointer));
        fileHandle.appendPage(newPage);
    }

    // Creates fully formatted record w/ header (null byte array, length of fields, and data)
    void *RecordBasedFileManager::createRecord(const std::vector<Attribute> &recordDescriptor, const void *data, size_t &recordSize) {
        // Calculate the number of fields and the size of the null-indicator bytes
        size_t numFields = recordDescriptor.size();
        size_t nullIndicatorSize = static_cast<size_t>(ceil(static_cast<double>(numFields) / 8));

        // Calculate the exact size of the record
        size_t size = nullIndicatorSize;
        const char *dataPtr = static_cast<const char *>(data);
        const char *nullIndicator = dataPtr;
        dataPtr += nullIndicatorSize;

        for (size_t i = 0; i < numFields; ++i) {
            // Check if the field is NULL
            size_t bytePos = i / 8;
            size_t bitPos = 7 - (i % 8);
            bool isNull = nullIndicator[bytePos] & (1 << bitPos);
            if (isNull) {
                continue;
            }

            // Calculate size based on field type as Q7 wants
            AttrType fieldType = recordDescriptor[i].type;
            if (fieldType == TypeInt || fieldType == TypeReal) {
                size += 4;
                dataPtr += 4;
            } else if (fieldType == TypeVarChar) {
                uint32_t varcharLength;
                memcpy(&varcharLength, dataPtr, sizeof(uint32_t));
                size += 4 + varcharLength; // 4 bytes for length + actual string size
                dataPtr += 4 + varcharLength; // Move pointer past length and string
            }
        }

        // Record size calculation complete
        recordSize = size;

        // Allocate memory for the formatted record
        char *formattedRecord = new char[recordSize];

        // Write the record data
        dataPtr = static_cast<const char *>(data);
        nullIndicator = dataPtr;
        memcpy(formattedRecord, nullIndicator, nullIndicatorSize);
        dataPtr += nullIndicatorSize;

        // Track the current offset in the formatted record
        size_t offset = nullIndicatorSize;

        for (size_t i = 0; i < numFields; ++i) {
            // Check if the field is NULL
            size_t bytePos = i / 8;
            size_t bitPos = 7 - (i % 8);
            bool isNull = nullIndicator[bytePos] & (1 << bitPos);
            if (isNull) {
                continue;
            }

            // Write field data based on type as Q7 wants
            AttrType fieldType = recordDescriptor[i].type;
            if (fieldType == TypeInt || fieldType == TypeReal) {
                memcpy(formattedRecord + offset, dataPtr, 4);
                offset += 4;
                dataPtr += 4;
            } else if (fieldType == TypeVarChar) {
                uint32_t varcharLength;
                memcpy(&varcharLength, dataPtr, sizeof(uint32_t));
                memcpy(formattedRecord + offset, dataPtr, 4 + varcharLength);
                offset += 4 + varcharLength;
                dataPtr += 4 + varcharLength;
            }
        }

        return formattedRecord;
    }

    // Get amount of free space given record size
    unsigned RecordBasedFileManager::getInsertRecordPage(FileHandle &fileHandle, const size_t recordSize) {
        unsigned totalPages = fileHandle.getNumberOfPages();
        // Check the last page first
        if (totalPages > 0) {
            unsigned lastPage = totalPages - 1;
            if (hasSpaceInPage(fileHandle, lastPage, recordSize)) {
                return lastPage;
            }
        }
        // Check others
        for (unsigned pageNum = 0; pageNum < totalPages - 1; pageNum++) {
            if (hasSpaceInPage(fileHandle, pageNum, recordSize)) {
                return pageNum;
            }
        }
        // No fit found, append new page
        appendRecordPage(fileHandle);
        return totalPages;
    }

    // Helper function to determine if a page has enough space for the record (remember to consider growth of the slot directory 2)
    bool RecordBasedFileManager::hasSpaceInPage(FileHandle &fileHandle, unsigned pageNum, const size_t recordSize) {
        char page[PAGE_SIZE];
        fileHandle.readPage(pageNum, page);

        unsigned numSlots;
        unsigned freeSpaceOffset;
        memcpy(&numSlots, page + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned)); // Number of slots
        memcpy(&freeSpaceOffset, page + PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE, sizeof(unsigned)); // Free space offset

        // Calculate remaining free space
        unsigned slottedDirectoryStart = PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE - (numSlots * SLOT_DIRECTORY_ENTRY_SIZE);
        unsigned freeSpace = slottedDirectoryStart - freeSpaceOffset;

        // Check if there is a reusable slot
        bool reusableSlotFound = false;
        for (unsigned i = 0; i < numSlots; ++i) {
            unsigned slotOffset = PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE - (i + 1) * SLOT_DIRECTORY_ENTRY_SIZE;
            unsigned recordOffset;
            int recordLength;
            memcpy(&recordOffset, page + slotOffset, sizeof(unsigned));
            memcpy(&recordLength, page + slotOffset + sizeof(unsigned), sizeof(unsigned));

            // A slot is reusable if both its offset and length are 0
            if (recordOffset == 0 && recordLength == 0) {
                reusableSlotFound = true;
                break;
            }
        }

        // Calculate the required space
        unsigned requiredSpace = recordSize;
        if (!reusableSlotFound) {
            // Include the space needed for a new slot entry if no reusable slot is available
            requiredSpace += SLOT_DIRECTORY_ENTRY_SIZE;
        }

        return freeSpace >= requiredSpace;
    }

    void RecordBasedFileManager::insertRecordIntoPage(FileHandle &fileHandle, unsigned pageNum, size_t recordSize, const void *recordData, RID &rid) {
        char page[PAGE_SIZE];
        fileHandle.readPage(pageNum, page);

        // Extract the number of slots and free space offset from the slotted directory
        int numSlots;
        int freeSpaceOffset;
        memcpy(&numSlots, page + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned)); // Number of slots
        memcpy(&freeSpaceOffset, page + PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE, sizeof(unsigned)); // Free space offset

        bool reusableSlotFound = false;
        int reusedSlotOffset = 0;
        int reusedSlotNum = 0;

        // Iterate through the slot directory to check for a reusable slot (offset and size both 0)
        for (int i = 0; i < numSlots; ++i) {
            int slotOffset = PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE - (i + 1) * SLOT_DIRECTORY_ENTRY_SIZE;
            int recordOffset;
            int recordLength;

            // Read the offset and size of the current slot
            memcpy(&recordOffset, page + slotOffset, sizeof(unsigned));
            memcpy(&recordLength, page + slotOffset + sizeof(unsigned), sizeof(unsigned));

            // A slot is reusable if both its offset and length are 0
            if (recordOffset == 0 && recordLength == 0) {
                reusableSlotFound = true;
                reusedSlotOffset = slotOffset;
                reusedSlotNum = i;
                break;  // Exit the loop once a reusable slot is found
            }
        }

        if (reusableSlotFound) {
            // Reuse the slot: Update the slot directory with the new offset and size
            memcpy(page + reusedSlotOffset, &freeSpaceOffset, sizeof(unsigned)); // New offset
            memcpy(page + reusedSlotOffset + sizeof(unsigned), &recordSize, sizeof(unsigned)); // New size

            // Set the RID for the reused record
            rid.pageNum = pageNum;
            rid.slotNum = reusedSlotNum; // Set the correct slot number
        } else {
            // If no slot is reused, create a new slot at the leftmost position
            int newSlotOffset = PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE - (numSlots + 1) * SLOT_DIRECTORY_ENTRY_SIZE; // New slot at the beginning of the directory

            // Update the slot directory with the new slot (record offset and size)
            memcpy(page + newSlotOffset, &freeSpaceOffset, sizeof(unsigned)); // Record offset
            memcpy(page + newSlotOffset + sizeof(unsigned), &recordSize, sizeof(unsigned)); // Record size

            // Increment the number of slots (the new slot takes the "highest" index)
            numSlots++;

            // Set the RID for the new record
            rid.pageNum = pageNum;
            rid.slotNum = numSlots - 1;
        }

        // Copy the record into the free space
        memcpy(page + freeSpaceOffset, recordData, recordSize);

        // Update the free space offset
        freeSpaceOffset += recordSize;

        // Write the updated metadata back to the page
        memcpy(page + PAGE_SIZE - sizeof(unsigned), &numSlots, sizeof(unsigned)); // Update number of slots
        memcpy(page + PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE, &freeSpaceOffset, sizeof(unsigned)); // Update free space offset
        // Write the updated page back to the file
        fileHandle.writePage(pageNum, page);
    }

    RC RecordBasedFileManager::readRecord(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor,
                                      const RID &rid, void *data) {
        // Check if it's a tombstone (redirecting to another RID)
        RID nextRID;
        if (isTombstone(fileHandle, rid, nextRID) == 1) {
            return readRecord(fileHandle, recordDescriptor, nextRID, data);
        }

        char page[PAGE_SIZE];
        if (fileHandle.readPage(rid.pageNum, page) != 0) {
            return -1;
        }

        // Metadata
        int recordOffset, recordLength;
        int slotOffset = PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE - (rid.slotNum + 1) * SLOT_DIRECTORY_ENTRY_SIZE;
        memcpy(&recordOffset, page + slotOffset, sizeof(unsigned));
        memcpy(&recordLength, page + slotOffset + sizeof(unsigned), sizeof(unsigned));

        // If the record is deleted or is a tombstone, return an error
        if (recordOffset == 0 && recordLength == 0) {
            return -1;
        }

        // Read the record data from the page
        memcpy(data, page + recordOffset, recordLength);
        return 0;
    }

    RC RecordBasedFileManager::printRecord(const std::vector<Attribute> &recordDescriptor, const void *data,
                                       std::ostream &out) {
        size_t numFields = recordDescriptor.size();
        size_t nullIndicatorSize = ceil((double)numFields / 8);

        const char *dataPtr = (const char *)data;
        const char *nullFieldsIndicator = dataPtr;
        dataPtr += nullIndicatorSize;

        for (size_t i = 0; i < numFields; ++i) {
            // Print attribute name
            out << recordDescriptor[i].name << ": ";

            // Check if the field is NULL
            if (nullFieldsIndicator[i / 8] & (1 << (7 - (i % 8)))) {
                out << "NULL";
            } else {
                try {
                    if (recordDescriptor[i].type == TypeInt) {
                        int value;
                        memcpy(&value, dataPtr, sizeof(int));
                        out << value;
                        dataPtr += sizeof(int);
                    } else if (recordDescriptor[i].type == TypeReal) {
                        float value;
                        memcpy(&value, dataPtr, sizeof(float));
                        out << value;
                        dataPtr += sizeof(float);
                    } else if (recordDescriptor[i].type == TypeVarChar) {
                        int varcharLength;
                        memcpy(&varcharLength, dataPtr, sizeof(int));
                        dataPtr += sizeof(int);

                        std::string value(dataPtr, varcharLength);
                        out << value;
                        dataPtr += varcharLength;
                    }
                } catch (const std::exception &e) {
                    std::cerr << "Exception while printing record: " << e.what() << std::endl;
                    return -1; // Return error code if an exception occurs
                }
            }

            // Print comma or newline
            if (i < numFields - 1) {
                out << ", ";
            } else {
                out << "\n";
            }
        }
        return 0;
    }

    RC RecordBasedFileManager::deleteRecord(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor,
                                        const RID &rid) {
        RID currentRID = rid;
        RID nextRID;
        // Loop through tombstones
        while (isTombstone(fileHandle, currentRID, nextRID) == 1) {
            if (deleteTombstone(fileHandle, currentRID) == -1) {
                return -1;
            }
            currentRID.pageNum = nextRID.pageNum;
            currentRID.slotNum = nextRID.slotNum;
        }

        char page[PAGE_SIZE];
        fileHandle.readPage(currentRID.pageNum, page);
        // Current record is the actual record; delete it
        unsigned freeSpaceOffset, numSlots;
        memcpy(&freeSpaceOffset, page + PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE, sizeof(unsigned));
        memcpy(&numSlots, page + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned));

        int slotOffset = PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE - (currentRID.slotNum + 1) * SLOT_DIRECTORY_ENTRY_SIZE;
        int recordOffset, recordLength = 0;
        memcpy(&recordOffset, page + slotOffset, sizeof(unsigned));
        memcpy(&recordLength, page + slotOffset + sizeof(unsigned), sizeof(unsigned));

        // Mark the record's slot as deleted
        int zero = 0;
        memcpy(page + slotOffset, &zero, sizeof(unsigned));
        memcpy(page + slotOffset + sizeof(unsigned), &zero, sizeof(unsigned));

        // Move records located after the deleted record to the left
        unsigned recordEnd = recordOffset + recordLength;
        unsigned moveSize = freeSpaceOffset - recordEnd;
        if (moveSize > 0) {
            memmove(page + recordOffset, page + recordEnd, moveSize);
            // Update slot directory offsets for records after the deleted record
            for (int i = 0; i < numSlots; ++i) {
                int currentSlotOffset = PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE - (i + 1) * SLOT_DIRECTORY_ENTRY_SIZE;
                int currentOffset;
                memcpy(&currentOffset, page + currentSlotOffset, sizeof(unsigned));

                // Adjust offsets for records that were shifted left
                if (currentOffset > recordOffset) {
                    int newOffset = currentOffset - recordLength;
                    memcpy(page + currentSlotOffset, &newOffset, sizeof(unsigned));
                }
            }

            // Update the free space offset
            freeSpaceOffset -= recordLength;
            memcpy(page + PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE, &freeSpaceOffset, sizeof(unsigned));
        }
        // Write the updated page back to the file
        if (fileHandle.writePage(currentRID.pageNum, page) != 0) {
            return -1;
        }
        return 0;
    }

    RC RecordBasedFileManager::isTombstone(FileHandle &fileHandle, const RID &inputRID, RID &outputRID) {
        char page[PAGE_SIZE];
        if (fileHandle.readPage(inputRID.pageNum, page) != 0) {
            return -1;
        }

        // Retrieve slot information
        int recordOffset, recordLength;
        unsigned slotOffset = PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE - (inputRID.slotNum + 1) * SLOT_DIRECTORY_ENTRY_SIZE;
        memcpy(&recordOffset, page + slotOffset, sizeof(unsigned));
        memcpy(&recordLength, page + slotOffset + sizeof(unsigned), sizeof(int));

        // Check if the record is a tombstone
        if (recordLength == -1) {
            memcpy(&outputRID.pageNum, page + recordOffset, sizeof(unsigned));
            memcpy(&outputRID.slotNum, page + recordOffset + sizeof(unsigned), sizeof(unsigned));
            return 1;
        }
        // No tombstone, the input RID corresponds to a valid record
        return 0;
    }

    RC RecordBasedFileManager::deleteTombstone(FileHandle &fileHandle, const RID &rid) {
        char page[PAGE_SIZE];

        // Read the page containing the tombstone
        if (fileHandle.readPage(rid.pageNum, page) != 0) {
            return -1;
        }

        // Retrieve the slot directory for the tombstone
        int slotOffset = PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE - (rid.slotNum + 1) * SLOT_DIRECTORY_ENTRY_SIZE;
        int recordOffset;
        int recordLength = 2 * sizeof(unsigned);
        memcpy(&recordOffset, page + slotOffset, sizeof(unsigned));

        // Update the slot to mark it as empty
        int zero = 0;
        memcpy(page + slotOffset, &zero, sizeof(unsigned));
        memcpy(page + slotOffset + sizeof(unsigned), &zero, sizeof(unsigned));

        // Shift records located after the tombstone left
        int freeSpaceOffset, numSlots;
        memcpy(&freeSpaceOffset, page + PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE, sizeof(unsigned));
        memcpy(&numSlots, page + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned));

        unsigned recordEnd = recordOffset + recordLength;
        unsigned moveSize = freeSpaceOffset - recordEnd;

        if (moveSize > 0) {
            memmove(page + recordOffset, page + recordEnd, moveSize);
            for (unsigned i = 0; i < numSlots; ++i) {
                unsigned currentSlotOffset = PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE - (i + 1) * SLOT_DIRECTORY_ENTRY_SIZE;
                int currentOffset;
                memcpy(&currentOffset, page + currentSlotOffset, sizeof(unsigned));
                // Adjust offsets for records that were shifted left
                if (currentOffset > recordOffset) {
                    unsigned newOffset = currentOffset - recordLength;
                    memcpy(page + currentSlotOffset, &newOffset, sizeof(unsigned));
                }
            }
            // Update the free space offset
            freeSpaceOffset -= recordLength;
            memcpy(page + PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE, &freeSpaceOffset, sizeof(unsigned));
        }

        // Write the updated page back to the file
        if (fileHandle.writePage(rid.pageNum, page) != 0) {
            return -1;
        }
        return 0;
    }

    RC RecordBasedFileManager::updateRecord(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor, const void *data, const RID &rid) {
        char page[PAGE_SIZE];
        RID currentRID = rid;
        RID nextRID;
        if (fileHandle.readPage(currentRID.pageNum, page) != 0) {
            return -1;
        }

        while (isTombstone(fileHandle, currentRID, nextRID) == 1) {
            currentRID.pageNum = nextRID.pageNum;
            currentRID.slotNum = nextRID.slotNum;
        }

        // Gather metadata
        fileHandle.readPage(currentRID.pageNum, page);
        int recordOffset, recordLength, numSlots, freeSpaceOffset;
        int slotOffset = PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE - (currentRID.slotNum + 1) * SLOT_DIRECTORY_ENTRY_SIZE;
        memcpy(&recordOffset, page + slotOffset, sizeof(unsigned));
        memcpy(&recordLength, page + slotOffset + sizeof(unsigned), sizeof(unsigned));
        memcpy(&numSlots, page + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned));
        memcpy(&freeSpaceOffset, page + PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE, sizeof(unsigned));

        // Create the new record and calculate its size
        size_t updatedRecordSize;
        void *updatedRecord = createRecord(recordDescriptor, data, updatedRecordSize);

        // Check if the updated record size fits in the same location
        if (updatedRecordSize <= recordLength) {
            memcpy(page + recordOffset, updatedRecord, updatedRecordSize);
            memcpy(page + slotOffset + sizeof(unsigned), &updatedRecordSize, sizeof(unsigned));
            int moveSize = recordLength - updatedRecordSize;
            int shiftSize = freeSpaceOffset - (recordOffset + recordLength);
            if (shiftSize > 0) {
                // Shift records to the left to reclaim free space
                memmove(page + recordOffset + updatedRecordSize, page + recordOffset + recordLength, shiftSize);

                // Update the free space offset
                freeSpaceOffset -= moveSize;
                memcpy(page + PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE, &freeSpaceOffset, sizeof(unsigned));

                // Adjust the slot directory for all records that were shifted
                for (unsigned i = 0; i < numSlots; ++i) {
                    int currentSlotOffset = PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE - (i + 1) * SLOT_DIRECTORY_ENTRY_SIZE;
                    int currentOffset;
                    int currentLength;
                    memcpy(&currentOffset, page + currentSlotOffset, sizeof(unsigned));
                    memcpy(&currentLength, page + currentSlotOffset + sizeof(unsigned), sizeof(unsigned));

                    if (currentOffset > recordOffset) {
                        int newOffset = currentOffset - moveSize;
                        memcpy(page + currentSlotOffset, &newOffset, sizeof(unsigned));
                    }
                }
            }
        } else {
            // Handle cases for larger record sizes or tombstones
            int availableSpace = PAGE_SIZE - freeSpaceOffset - (SLOT_DIRECTORY_ENTRY_SIZE * (numSlots + 1)) + recordLength;
            if (updatedRecordSize <= availableSpace) {
                // Case: Record fits on the same page
                int shiftSize = updatedRecordSize - recordLength;
                int moveSize = freeSpaceOffset - (recordOffset + recordLength);

                // Shift records to the right
                if (moveSize > 0) {
                    memmove(page + recordOffset + updatedRecordSize,
                            page + recordOffset + recordLength,
                            moveSize);
                }

                // Write the updated record
                memcpy(page + recordOffset, updatedRecord, updatedRecordSize);

                // Update the free space offset
                freeSpaceOffset += shiftSize;
                memcpy(page + PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE, &freeSpaceOffset, sizeof(unsigned));

                // Adjust the slot directory for all records that were moved
                for (int i = 0; i < numSlots; ++i) {
                    int currentSlotOffset = PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE - (i + 1) * SLOT_DIRECTORY_ENTRY_SIZE;
                    int currentOffset;
                    int currentLength;
                    memcpy(&currentOffset, page + currentSlotOffset, sizeof(unsigned));
                    memcpy(&currentLength, page + currentSlotOffset + sizeof(unsigned), sizeof(unsigned));

                    if (currentOffset > recordOffset) {
                        int newOffset = currentOffset + shiftSize;
                        memcpy(page + currentSlotOffset, &newOffset, sizeof(unsigned));
                    }
                }

                // Update the slot directory entry for the current record
                memcpy(page + slotOffset + sizeof(unsigned), &updatedRecordSize, sizeof(unsigned));
            } else {
                // Insert the updated record into a new location
                RID newRID;
                insertRecord(fileHandle, recordDescriptor, updatedRecord, newRID);
                fileHandle.readPage(currentRID.pageNum, page);

                // Store the tombstone reference in place of the old record
                memcpy(page + recordOffset, &newRID.pageNum, sizeof(unsigned));
                memcpy(page + recordOffset + sizeof(unsigned), &newRID.slotNum, sizeof(unsigned));
                // Correctly mark the slot as a tombstone (-1 length)
                int tombstoneIndicator = -1;
                memcpy(page + slotOffset + sizeof(unsigned), &tombstoneIndicator, sizeof(unsigned));
                // Shift memory left only if necessary
                size_t tombstoneSize = 2 * sizeof(unsigned);
                int moveSize = recordLength - tombstoneSize;
                int shiftSize = freeSpaceOffset - (recordOffset + recordLength);
                if (moveSize > 0) {
                    memmove(page + recordOffset + tombstoneSize, page + recordOffset + recordLength, shiftSize);
                    // Update free space offset
                    freeSpaceOffset -= moveSize;
                    memcpy(page + PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE, &freeSpaceOffset, sizeof(unsigned));
                    // Adjust slot directory for records that were shifted
                    for (unsigned i = 0; i < numSlots; ++i) {
                        int currentSlotOffset = PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE - (i + 1) * SLOT_DIRECTORY_ENTRY_SIZE;
                        int currentOffset;
                        memcpy(&currentOffset, page + currentSlotOffset, sizeof(unsigned));

                        if (currentOffset > recordOffset) {
                            int newOffset = currentOffset - moveSize;
                            memcpy(page + currentSlotOffset, &newOffset, sizeof(unsigned));
                        }
                    }
                }
            }
        }
        free(updatedRecord);
        if (fileHandle.writePage(currentRID.pageNum, page) != 0) {
            return -1;
        }
        // closeFile(fileHandle);
        return 0;
    }

    RC RecordBasedFileManager::readAttribute(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor,
                                         const RID &rid, const std::string &attributeName, void *data) {
        char recordData[PAGE_SIZE];

        // Read the full record
        if (readRecord(fileHandle, recordDescriptor, rid, recordData) != 0) {
            return -1;
        }

        // Null indicator size calculation
        size_t nullIndicatorSize = ceil((double)recordDescriptor.size() / 8.0);
        const char *nullIndicator = recordData;
        const char *dataPtr = (const char *)recordData + nullIndicatorSize;

        // Iterate through the attributes to locate the desired attribute
        for (size_t i = 0; i < recordDescriptor.size(); ++i) {
            const Attribute &attribute = recordDescriptor[i];
            if (attributeName == attribute.name) {
                // Check if the attribute is NULL
                unsigned nullByteIndex = i / 8;
                unsigned nullBitIndex = 7 - (i % 8);
                if (nullIndicator[nullByteIndex] & (1 << nullBitIndex)) {
                    char nullByte = 0x80; // 10000000 in binary
                    memcpy(data, &nullByte, 1);
                    return 0;
                }
                if (attribute.type == TypeInt) {
                    int intValue;
                    memcpy(&intValue, dataPtr, sizeof(int));
                    // Set null indicator byte to 0
                    memset(data, 0, 1);
                    memcpy(static_cast<char*>(data) + 1, &intValue, sizeof(int));
                } else if (attribute.type == TypeReal) {
                    float floatValue;
                    memcpy(&floatValue, dataPtr, sizeof(float));
                    // Set null indicator byte to 0
                    memset(data, 0, 1);
                    memcpy(static_cast<char*>(data) + 1, &floatValue, sizeof(float));
                } else if (attribute.type == TypeVarChar) {
                    unsigned stringLength;
                    memcpy(&stringLength, dataPtr, sizeof(unsigned));
                    dataPtr += sizeof(unsigned);
                    // Set null indicator byte to 0
                    memset(data, 0, 1);
                    memcpy(static_cast<char*>(data) + 1, &stringLength, sizeof(unsigned));
                    memcpy(static_cast<char*>(data) + 1 + sizeof(unsigned), dataPtr, stringLength);
                }
                return 0;  // Attribute found and value extracted
            } else {
                unsigned nullByteIndex = i / 8;
                unsigned nullBitIndex = 7 - (i % 8);
                if (nullIndicator[nullByteIndex] & (1 << nullBitIndex)) {
                    continue;
                }
                // Move offset to next attribute
                if (attribute.type == TypeInt) {
                    dataPtr += sizeof(int);
                }
                else if (attribute.type == TypeReal) {
                    dataPtr += sizeof(float);
                } else if (attribute.type == TypeVarChar) {
                    unsigned stringLength;
                    memcpy(&stringLength, dataPtr, sizeof(unsigned));
                    dataPtr += sizeof(unsigned) + stringLength;
                }
            }
        }
        return -1;
    }

    RC RBFM_ScanIterator::initialize(std::string fileName) {
        this->currentPage = 0;
        this->currentSlot = 0;
        this->fileName = std::move(fileName);
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        rbfm.createFile(this->fileName);
        this->fileHandle = new FileHandle();
        rbfm.openFile(this->fileName, *this->fileHandle);
        return 0;
    }

    RC RBFM_ScanIterator::setRecordDescriptor(const std::vector<Attribute> &recordDescriptor) {
        this->recordDescriptor = recordDescriptor;
        return 0;
    }

    RC RBFM_ScanIterator::insertData(const void *data, RID &rid, const std::vector<Attribute> &filteredRecordDescriptor) {
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        rbfm.insertRecord(*fileHandle, filteredRecordDescriptor, data, rid);
        return 0;
    }

    RC RBFM_ScanIterator::getNextRecord(RID &rid, void *data) {
        RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
        while (currentPage < fileHandle->getNumberOfPages()) {
            char pageData[PAGE_SIZE];
            if (fileHandle->readPage(currentPage, pageData) != 0) {
                return -1;
            }
            // Get the number of slots in the page
            unsigned numSlots;
            memcpy(&numSlots, pageData + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned));
            while (currentSlot < numSlots) {
                unsigned slotOffset = PAGE_SIZE - SLOT_DIRECTORY_ENTRY_SIZE - (currentSlot + 1) * SLOT_DIRECTORY_ENTRY_SIZE;
                unsigned recordOffset, recordLength;
                memcpy(&recordOffset, pageData + slotOffset, sizeof(unsigned));
                memcpy(&recordLength, pageData + slotOffset + sizeof(unsigned), sizeof(unsigned));
                // Build the RID
                rid.pageNum = currentPage;
                rid.slotNum = currentSlot;
                // Read the record
                if (rbfm.readRecord(*fileHandle, this->recordDescriptor, rid, data) == 0) {
                    currentSlot++;
                    return 0;
                }
                // If readRecord fails, still move to the next slot
                currentSlot++;
            }
            // Move to the next page
            currentPage++;
            currentSlot = 0;
        }
        return RBFM_EOF;
    }

    RC RBFM_ScanIterator::close() {
        if (fileHandle) {
            RecordBasedFileManager &rbfm = RecordBasedFileManager::instance();
            rbfm.closeFile(*fileHandle);
            rbfm.destroyFile(fileName);
            delete fileHandle;
            fileHandle = nullptr;
            currentPage = 0;
            currentSlot = 0;
            recordDescriptor.clear();
            fileName.clear();
        }
        return 0;
    }

    RC RecordBasedFileManager::scan(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor,
                                const std::string &conditionAttribute, const CompOp compOp, const void *value,
                                const std::vector<std::string> &attributeNames,
                                RBFM_ScanIterator &rbfm_ScanIterator) {
        // Filter the record descriptor to only include requested attributes
        std::vector<Attribute> filteredRecordDescriptor;
        // Order matters
        for (const auto &attrName : attributeNames) {
            auto it = std::find_if(recordDescriptor.begin(), recordDescriptor.end(), [&](const Attribute &attr) { return attr.name == attrName; });
            if (it != recordDescriptor.end()) {
                filteredRecordDescriptor.push_back(*it);
            }
        }
        rbfm_ScanIterator.setRecordDescriptor(filteredRecordDescriptor);

        // Iterate over all pages
        unsigned numPages = fileHandle.getNumberOfPages();
        for (unsigned pageNum = 0; pageNum < numPages; ++pageNum) {
            char pageData[PAGE_SIZE];
            if (fileHandle.readPage(pageNum, pageData) != 0) {
                return -1;  // Error reading page
            }

            // Get the number of slots
            unsigned numSlots;
            memcpy(&numSlots, pageData + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned));
            for (unsigned short slotNum = 0; slotNum < numSlots; slotNum++) {
                RID rid = {pageNum, slotNum};
                char recordData[PAGE_SIZE];

                // Skip tombstones
                RID nextRID;
                if (isTombstone(fileHandle, rid, nextRID) != 0) {
                    continue;
                }

                // Read the record
                if (readRecord(fileHandle, recordDescriptor, rid, recordData) != 0) {
                    continue;  // Skip invalid records
                }

                // Apply condition filtering
                if (!checkCondition(recordData, recordDescriptor, conditionAttribute, compOp, value)) {
                    continue;  // Skip records that do not satisfy the condition
                }

                // Create a filtered record
                size_t newRecordSize = 0;
                void *filteredRecord = createFilteredRecord(recordDescriptor, filteredRecordDescriptor, recordData, newRecordSize);

                // Insert the filtered record into the iterator
                rbfm_ScanIterator.insertData(filteredRecord, rid, filteredRecordDescriptor);

                // Cleanup allocated memory
                delete[] static_cast<char *>(filteredRecord);
            }
        }

        return 0;
    }

    void *RecordBasedFileManager::createFilteredRecord(const std::vector<Attribute> &recordDescriptor,
                                                   const std::vector<Attribute> &filteredRecordDescriptor,
                                                   const void *recordData, size_t &newRecordSize) {
        size_t numFields = filteredRecordDescriptor.size();
        size_t nullIndicatorSize = static_cast<size_t>(ceil(static_cast<double>(numFields) / 8));
        size_t fullRecordNullIndicatorSize = static_cast<size_t>(ceil(static_cast<double>(recordDescriptor.size()) / 8));

        const char *dataPtr = static_cast<const char *>(recordData);
        const char *nullIndicator = dataPtr;
        dataPtr += fullRecordNullIndicatorSize;

        // Allocate memory for the new record
        char *filteredRecord = new char[PAGE_SIZE];  // Temporary large buffer
        memset(filteredRecord, 0, PAGE_SIZE);  // Ensure clean memory
        char *filteredPtr = filteredRecord;

        // Copy new null indicator
        memset(filteredPtr, 0, nullIndicatorSize);
        filteredPtr += nullIndicatorSize;

        // Track current offset within recordData
        std::vector<size_t> fieldOffsets(recordDescriptor.size(), 0);
        size_t currentOffset = fullRecordNullIndicatorSize;

        for (size_t j = 0; j < recordDescriptor.size(); ++j) {
            size_t bytePos = j / 8;
            size_t bitPos = 7 - (j % 8);
            bool isNull = nullIndicator[bytePos] & (1 << bitPos);

            if (isNull) {
                fieldOffsets[j] = currentOffset;  // NULL fields still have a logical position
                continue;
            }

            if (recordDescriptor[j].type == TypeInt || recordDescriptor[j].type == TypeReal) {
                fieldOffsets[j] = currentOffset;
                currentOffset += sizeof(int);
            } else if (recordDescriptor[j].type == TypeVarChar) {
                unsigned stringLength;
                memcpy(&stringLength, dataPtr + (currentOffset - fullRecordNullIndicatorSize), sizeof(unsigned));
                fieldOffsets[j] = currentOffset;
                currentOffset += sizeof(unsigned) + stringLength;
            }
        }

        // Extract required fields
        for (size_t i = 0; i < numFields; ++i) {
            const Attribute &filteredAttr = filteredRecordDescriptor[i];

            // **Find the original index based on the attribute name**
            auto it = std::find_if(recordDescriptor.begin(), recordDescriptor.end(),
                                   [&](const Attribute &attr) { return attr.name == filteredAttr.name; });

            if (it == recordDescriptor.end()) continue;  // Safety check
            size_t originalIndex = std::distance(recordDescriptor.begin(), it);

            // Check if the corresponding field is NULL
            size_t bytePos = originalIndex / 8;
            size_t bitPos = 7 - (originalIndex % 8);
            bool isNull = nullIndicator[bytePos] & (1 << bitPos);

            // Set NULL bit in the new record's null indicator
            size_t filteredBytePos = i / 8;
            size_t filteredBitPos = 7 - (i % 8);
            if (isNull) {
                filteredRecord[filteredBytePos] |= (1 << filteredBitPos);
                continue;
            }

            // Retrieve correct offset for this field
            const char *fieldDataPtr = static_cast<const char *>(recordData) + fieldOffsets[originalIndex];

            // Copy field data based on type
            if (filteredAttr.type == TypeInt || filteredAttr.type == TypeReal) {
                memcpy(filteredPtr, fieldDataPtr, sizeof(int));
                filteredPtr += sizeof(int);
            } else if (filteredAttr.type == TypeVarChar) {
                unsigned stringLength;
                memcpy(&stringLength, fieldDataPtr, sizeof(unsigned));
                fieldDataPtr += sizeof(unsigned);

                memcpy(filteredPtr, &stringLength, sizeof(unsigned));
                filteredPtr += sizeof(unsigned);

                memcpy(filteredPtr, fieldDataPtr, stringLength);
                filteredPtr += stringLength;
            }
        }

        // Set new record size
        newRecordSize = filteredPtr - filteredRecord;

        // Allocate exact-size memory and copy data
        char *finalFilteredRecord = new char[newRecordSize];
        memcpy(finalFilteredRecord, filteredRecord, newRecordSize);

        // Cleanup
        delete[] filteredRecord;

        return finalFilteredRecord;
    }




    bool RecordBasedFileManager::checkCondition(const void *recordData, const std::vector<Attribute> &recordDescriptor,
                                            const std::string &conditionAttribute, const CompOp compOp, const void *value)
    {
        if (compOp == NO_OP) {
            return true;
        }

        size_t numFields = recordDescriptor.size();
        size_t nullIndicatorSize = ceil((double)numFields / 8);
        const char *nullIndicator = static_cast<const char *>(recordData);

        const char *dataPtr = (const char *)recordData;
        dataPtr += nullIndicatorSize;  // Skip the null indicator bytes
        int i = 0;
        for (const auto &attribute : recordDescriptor) {
            int varCharLength = 0;
            if (attribute.name == conditionAttribute) {
                unsigned nullByteIndex = i / 8;
                unsigned nullBitIndex = 7 - (i % 8);
                if (nullIndicator[nullByteIndex] & (1 << nullBitIndex)) {
                    return false;
                }
                if (attribute.type == TypeInt) {
                    int recordValue, conditionValue;
                    memcpy(&recordValue, dataPtr, sizeof(int));
                    memcpy(&conditionValue, value, sizeof(int));
                    switch (compOp) {
                        case EQ_OP: return recordValue == conditionValue;
                        case LT_OP: return recordValue < conditionValue;
                        case LE_OP: return recordValue <= conditionValue;
                        case GT_OP: return recordValue > conditionValue;
                        case GE_OP: return recordValue >= conditionValue;
                        case NE_OP: return recordValue != conditionValue;
                        default: return true;
                    }
                }
                else if (attribute.type == TypeReal) {
                    float recordValue, conditionValue;
                    memcpy(&recordValue, dataPtr, sizeof(float));
                    memcpy(&conditionValue, value, sizeof(float));
                    switch (compOp) {
                        case EQ_OP: return recordValue == conditionValue;
                        case LT_OP: return recordValue < conditionValue;
                        case LE_OP: return recordValue <= conditionValue;
                        case GT_OP: return recordValue > conditionValue;
                        case GE_OP: return recordValue >= conditionValue;
                        case NE_OP: return recordValue != conditionValue;
                        default: return true;
                    }
                }
                else if (attribute.type == TypeVarChar) {
                    memcpy(&varCharLength, dataPtr, sizeof(unsigned));
                    std::string recordValue(dataPtr + sizeof(unsigned), varCharLength);
                    std::string conditionValue = (char*)value;
                    if (recordValue.empty()) {
                        return false;
                    }
                    switch (compOp) {
                        case EQ_OP: return recordValue == conditionValue;
                        case LT_OP: return recordValue < conditionValue;
                        case LE_OP: return recordValue <= conditionValue;
                        case GT_OP: return recordValue > conditionValue;
                        case GE_OP: return recordValue >= conditionValue;
                        case NE_OP: return recordValue != conditionValue;
                        default: return false;
                    }
                }
            }
            unsigned nullByteIndex = i / 8;
            unsigned nullBitIndex = 7 - (i % 8);
            if (nullIndicator[nullByteIndex] & (1 << nullBitIndex)) {
                i++;
                continue;
            }
            switch (attribute.type) {
                case TypeInt: dataPtr += sizeof(int); break;
                case TypeReal: dataPtr += sizeof(float); break;
                case TypeVarChar: memcpy(&varCharLength, dataPtr, sizeof(unsigned)); dataPtr += sizeof(unsigned) + varCharLength; break;
            }
            i++;
        }
        return false;
    }
} // namespace PeterDB
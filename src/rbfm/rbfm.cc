#include "../include/rbfm.h"
#include <cstring>
#include <cstdint>
#include <cmath>


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
        pfm.createFile(fileName);
        return 0;
    }

    RC RecordBasedFileManager::destroyFile(const std::string &fileName) {
        PagedFileManager &pfm = PagedFileManager::instance();
        pfm.destroyFile(fileName);
        return 0;
    }

    RC RecordBasedFileManager::openFile(const std::string &fileName, FileHandle &fileHandle) {
        PagedFileManager &pfm = PagedFileManager::instance();
        pfm.openFile(fileName, fileHandle);
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
        memcpy(&freeSpaceOffset, page + PAGE_SIZE - 2 * sizeof(unsigned), sizeof(unsigned)); // Free space offset

        // Calculate remaining free space
        unsigned slottedDirectoryStart = PAGE_SIZE - 2 * sizeof(unsigned) - (numSlots * 2 * sizeof(unsigned));
        unsigned freeSpace = slottedDirectoryStart - freeSpaceOffset;
        // Include the space needed for the new slot entry
        unsigned requiredSpace = recordSize + (2 * sizeof(unsigned));

        return freeSpace > requiredSpace;
    }

    void RecordBasedFileManager::insertRecordIntoPage(FileHandle &fileHandle, unsigned pageNum, size_t recordSize, const void *recordData, RID &rid) {
        char page[PAGE_SIZE];
        fileHandle.readPage(pageNum, page);

        // Extract the number of slots and free space offset from the slotted directory
        unsigned numSlots;
        unsigned freeSpaceOffset;
        memcpy(&numSlots, page + PAGE_SIZE - sizeof(unsigned), sizeof(unsigned)); // Number of slots
        memcpy(&freeSpaceOffset, page + PAGE_SIZE - 2 * sizeof(unsigned), sizeof(unsigned)); // Free space offset

        // Calculate the starting offset for the new slot
        unsigned slotDirectoryStart = PAGE_SIZE - sizeof(unsigned) * 2 - (numSlots * sizeof(unsigned) * 2);
        unsigned newSlotOffset = slotDirectoryStart - (sizeof(unsigned) * 2);

        // There must be enough space in the page for the record and the new slot entry
        if (freeSpaceOffset + recordSize > newSlotOffset) {
            throw std::runtime_error("Not enough space in the page to insert the record.");
        }

        // Copy the record into the free space
        memcpy(page + freeSpaceOffset, recordData, recordSize);

        // Update the slot directory with the new slot (record offset and size)
        memcpy(page + newSlotOffset, &freeSpaceOffset, sizeof(unsigned)); // Record offset
        memcpy(page + newSlotOffset + sizeof(unsigned), &recordSize, sizeof(unsigned)); // Record size

        freeSpaceOffset += recordSize;
        numSlots++;

        // Write the updated metadata back to the page
        memcpy(page + PAGE_SIZE - sizeof(unsigned), &numSlots, sizeof(unsigned)); // Update number of slots
        memcpy(page + PAGE_SIZE - 2 * sizeof(unsigned), &freeSpaceOffset, sizeof(unsigned)); // Update free space offset

        // Write the updated page back to the file
        fileHandle.writePage(pageNum, page);

        // Set the RID for the inserted record
        rid.pageNum = pageNum;
        rid.slotNum = numSlots; // Slot number is 0-based
    }


    RC RecordBasedFileManager::readRecord(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor,
                                          const RID &rid, void *data) {
        void *pageData = malloc(PAGE_SIZE);

        // Read the page containing the record
        if (fileHandle.readPage(rid.pageNum, pageData) != 0) {
            free(pageData);
            return -1;
        }

        // Retrieve slot information
        unsigned recordOffset, recordLength;
        unsigned slotOffset = PAGE_SIZE - 2 * sizeof(unsigned) - rid.slotNum * SLOT_DIRECTORY_ENTRY_SIZE;
        memcpy(&recordOffset, (char *)pageData + slotOffset, sizeof(unsigned));
        memcpy(&recordLength, (char *)pageData + slotOffset + sizeof(unsigned), sizeof(unsigned));

        // Copy the record into the data buffer
        memcpy(data, (char *)pageData + recordOffset, recordLength);

        free(pageData);
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

    // Not required yet
    RC RecordBasedFileManager::deleteRecord(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor,
                                            const RID &rid) {
        return -1;
    }

    RC RecordBasedFileManager::updateRecord(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor,
                                            const void *data, const RID &rid) {
        return -1;
    }

    RC RecordBasedFileManager::readAttribute(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor,
                                             const RID &rid, const std::string &attributeName, void *data) {
        return -1;
    }

    RC RecordBasedFileManager::scan(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor,
                                    const std::string &conditionAttribute, const CompOp compOp, const void *value,
                                    const std::vector<std::string> &attributeNames,
                                    RBFM_ScanIterator &rbfm_ScanIterator) {
        return -1;
    }

} // namespace PeterDB


#ifndef _rbfm_h_
#define _rbfm_h_

#include <vector>

#include "pfm.h"

namespace PeterDB {
    // Record ID
    typedef struct {
        unsigned pageNum;           // page number
        unsigned short slotNum;     // slot number in the page
    } RID;

    // Attribute
    typedef enum {
        TypeInt = 0, TypeReal, TypeVarChar
    } AttrType;

    typedef unsigned AttrLength;

    typedef struct Attribute {
        std::string name;  // attribute name
        AttrType type;     // attribute type
        AttrLength length; // attribute length
    } Attribute;

    // Comparison Operator (NOT needed for part 1 of the project)
    typedef enum {
        EQ_OP = 0, // no condition// =
        LT_OP,      // <
        LE_OP,      // <=
        GT_OP,      // >
        GE_OP,      // >=
        NE_OP,      // !=
        NO_OP       // no condition
    } CompOp;


    /********************************************************************
    * The scan iterator is NOT required to be implemented for Project 1 *
    ********************************************************************/

# define RBFM_EOF (-1)  // end of a scan operator

    //  RBFM_ScanIterator is an iterator to go through records
    //  The way to use it is like the following:
    //  RBFM_ScanIterator rbfmScanIterator;
    //  rbfm.open(..., rbfmScanIterator);
    //  while (rbfmScanIterator(rid, data) != RBFM_EOF) {
    //    process the data;
    //  }
    //  rbfmScanIterator.close();

    class RBFM_ScanIterator {
    public:
        RBFM_ScanIterator(std::string fileName): fileName(fileName), fileHandle(nullptr), currentPage(0), currentSlot(0) {}

        ~RBFM_ScanIterator() = default;
        // Added methods
        RC initialize(std::string fileName);
        RC setRecordDescriptor(const std::vector<Attribute> &recordDescriptor);
        RC insertData(const void *data, RID &rid, const std::vector<Attribute> &filteredRecordDescriptor);
        RC getNextRecord(RID &rid, void *data);
        RC close();
    // Added properties
    private:
        std::string fileName;
        FileHandle *fileHandle;
        unsigned currentPage;
        unsigned currentSlot;
        std::vector<Attribute> recordDescriptor;
    };

    class RecordBasedFileManager {
    public:
        static RecordBasedFileManager &instance();                          // Access to the singleton instance

        RC createFile(const std::string &fileName);                         // Create a new record-based file

        RC destroyFile(const std::string &fileName);                        // Destroy a record-based file

        RC openFile(const std::string &fileName, FileHandle &fileHandle);   // Open a record-based file

        RC closeFile(FileHandle &fileHandle);                               // Close a record-based file

        //  Format of the data passed into the function is the following:
        //  [n byte-null-indicators for y fields] [actual value for the first field] [actual value for the second field] ...
        //  1) For y fields, there is n-byte-null-indicators in the beginning of each record.
        //     The value n can be calculated as: ceil(y / 8). (e.g., 5 fields => ceil(5 / 8) = 1. 12 fields => ceil(12 / 8) = 2.)
        //     Each bit represents whether each field value is null or not.
        //     If k-th bit from the left is set to 1, k-th field value is null. We do not include anything in the actual data part.
        //     If k-th bit from the left is set to 0, k-th field contains non-null values.
        //     If there are more than 8 fields, then you need to find the corresponding byte first,
        //     then find a corresponding bit inside that byte.
        //  2) Actual data is a concatenation of values of the attributes.
        //  3) For Int and Real: use 4 bytes to store the value;
        //     For Varchar: use 4 bytes to store the length of characters, then store the actual characters.
        //  !!! The same format is used for updateRecord(), the returned data of readRecord(), and readAttribute().
        // For example, refer to the Q8 of Project 1 wiki page.

        // Insert a record into a file
        RC insertRecord(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor, const void *data,
                        RID &rid);
        // Helpers
        void* createRecord(const std::vector<Attribute> &recordDescriptor, const void *data, size_t &recordSize); // Create formatted record from given info
        void appendRecordPage(FileHandle &fileHandle); // Append page w/ slot directory
        unsigned getInsertRecordPage(FileHandle &fileHandle, const size_t recordSize); // Figures out which page number will be used
        bool hasSpaceInPage(FileHandle &fileHandle, unsigned pageNum, const size_t recordSize); // Helper to above
        void insertRecordIntoPage(FileHandle &fileHandle, unsigned pageNum, size_t recordSize, const void *recordData, RID &rid); // Insertion function
        void log(const std::string &message);

        // Read a record identified by the given rid.
        RC
        readRecord(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor, const RID &rid, void *data);

        // Print the record that is passed to this utility method.
        // This method will be mainly used for debugging/testing.
        // The format is as follows:
        // field1-name: field1-value  field2-name: field2-value ... \n
        // (e.g., age: 24  height: 6.1  salary: 9000
        //        age: NULL  height: 7.5  salary: 7500)
        RC printRecord(const std::vector<Attribute> &recordDescriptor, const void *data, std::ostream &out);

        /*****************************************************************************************************
        * IMPORTANT, PLEASE READ: All methods below this comment (other than the constructor and destructor) *
        * are NOT required to be implemented for Project 1                                                   *
        *****************************************************************************************************/
        // Delete a record identified by the given rid.
        RC deleteRecord(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor, const RID &rid);

        RC deleteTombstone(FileHandle &fileHandle, const RID &rid);

        // Assume the RID does not change after an update
        RC updateRecord(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor, const void *data,
                        const RID &rid);

        // Checks if input RID is a tombstone, and if so returns 1 and fills the next RID into the output RID
        RC isTombstone(FileHandle &fileHandle, const RID &inputRID, RID &outputRID);

        // Read an attribute given its name and the rid.
        RC readAttribute(FileHandle &fileHandle, const std::vector<Attribute> &recordDescriptor, const RID &rid,
                         const std::string &attributeName, void *data);

        // Scan returns an iterator to allow the caller to go through the results one by one.
        RC scan(FileHandle &fileHandle,
                const std::vector<Attribute> &recordDescriptor,
                const std::string &conditionAttribute,
                const CompOp compOp,                  // comparison type such as "<" and "="
                const void *value,                    // used in the comparison
                const std::vector<std::string> &attributeNames, // a list of projected attributes
                RBFM_ScanIterator &rbfm_ScanIterator);

        void *createFilteredRecord(const std::vector<Attribute> &fullDescriptor,
                                   const std::vector<Attribute> &filteredDescriptor, const void *recordData,
                                   size_t &recordSize);

        bool checkCondition(const void *recordData, const std::vector<Attribute> &recordDescriptor,
                            const std::string &conditionAttribute, CompOp compOp, const void *value);

        // For QE filter file naming
        int getFilterFileNum() { return filterFileNum; }
        void incrementFilterFileNum() { filterFileNum++; }

        int getProjectFileNum() { return projectFileNum; }
        void incrementProjectFileNum() { projectFileNum++; }

        int getBNLJoinFIleNum() { return bnljoinFileNum; }
        void incrementBNLJoinFIleNum() { bnljoinFileNum++; }

        int getINLJoinFIleNum() { return inljoinFileNum; }
        void incrementINLJoinFIleNum() { inljoinFileNum++; }

    private:
        int filterFileNum = 0;
        int projectFileNum = 0;
        int bnljoinFileNum = 0;
        int inljoinFileNum = 0;

    protected:
        RecordBasedFileManager();                                                   // Prevent construction
        ~RecordBasedFileManager();                                                  // Prevent unwanted destruction
        RecordBasedFileManager(const RecordBasedFileManager &);                     // Prevent construction by copying
        RecordBasedFileManager &operator=(const RecordBasedFileManager &);          // Prevent assignment

    };

} // namespace PeterDB

#endif // _rbfm_h_

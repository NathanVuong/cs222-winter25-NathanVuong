#ifndef _ix_h_
#define _ix_h_

#include <vector>
#include <string>

#include "pfm.h"
#include "rbfm.h" // for some type declarations only, e.g., RID and Attribute

# define IX_EOF (-1)  // end of the index scan
# define PAGE_METADATA (sizeof(unsigned) * 3)

namespace PeterDB {
    class IX_ScanIterator;

    class IXFileHandle;

    class IndexManager {

    public:
        static IndexManager &instance();

        // Create an index file.
        RC createFile(const std::string &fileName);

        RC initializeIndex(IXFileHandle &ixFileHandle);

        // Delete an index file.
        RC destroyFile(const std::string &fileName);

        // Open an index and return an ixFileHandle.
        RC openFile(const std::string &fileName, IXFileHandle &ixFileHandle);

        // Close an ixFileHandle for an index.
        RC closeFile(IXFileHandle &ixFileHandle);

        unsigned getRootPageNum(IXFileHandle &ixFileHandle);

        RC setRootPageNum(IXFileHandle &ixFileHandle, unsigned newRootPageNum);

        bool isLeafNode(const char *pageData);

        unsigned getNumEntries(const char *pageData);

        unsigned getParentPageNum(const char *pageData);

        unsigned getLeafPageByKey(IXFileHandle &ixFileHandle, const Attribute &attribute, const void *key);

        void insertEntryIntoLeaf(char *pageData, unsigned spotToInsert, const Attribute &attribute, const void *key,
                                 const RID &rid, bool keyAlreadyExists, unsigned spaceTaken);

        void insertEntryIntoInternal(char *pageData, const Attribute &attribute, const void *key, unsigned leftPageNum,
                                     unsigned rightPageNum);

        unsigned calculateInternalSpaceTaken(char *pageData, const Attribute &attribute);

        // Insert an entry into the given index that is indicated by the given ixFileHandle.
        RC insertEntry(IXFileHandle &ixFileHandle, const Attribute &attribute, const void *key, const RID &rid);

        RC updateInternal(IXFileHandle &ixFileHandle, const Attribute &attribute, const void *key, unsigned leftPageNum,
                          unsigned rightPageNum, unsigned parentPageNum);

        // Delete an entry from the given index that is indicated by the given ixFileHandle.
        RC deleteEntry(IXFileHandle &ixFileHandle, const Attribute &attribute, const void *key, const RID &rid);

        // Initialize and IX_ScanIterator to support a range search
        RC scan(IXFileHandle &ixFileHandle,
                const Attribute &attribute,
                const void *lowKey,
                const void *highKey,
                bool lowKeyInclusive,
                bool highKeyInclusive,
                IX_ScanIterator &ix_ScanIterator);

        // Print the B+ tree in pre-order (in a JSON record format)
        RC printBTree(IXFileHandle &ixFileHandle, const Attribute &attribute, std::ostream &out) const;

        void printHelper(IXFileHandle &ixFileHandle, const Attribute &attribute, std::ostream &out, unsigned pageNum,
                         unsigned level) const;

    protected:
        IndexManager() = default;                                                   // Prevent construction
        ~IndexManager() = default;                                                  // Prevent unwanted destruction
        IndexManager(const IndexManager &) = default;                               // Prevent construction by copying
        IndexManager &operator=(const IndexManager &) = default;                    // Prevent assignment
    private:
        bool initialized = false;
        // static std::string printRIDs(const std::vector<RID>& rids);
    };

    class IX_ScanIterator {
    public:

        // Constructor
        IX_ScanIterator();

        // Destructor
        ~IX_ScanIterator();

        RC initialize(std::string fileName, const Attribute &attribute, IXFileHandle &ixFileHandle);

        RC insertScanEntry(const void *key, const RID &rid);

        // Get next matching entry
        RC getNextEntry(RID &rid, void *key);

        // Terminate index scan
        RC close();
        // Added properties
    private:
        std::string fileName;
        IXFileHandle *ixFileHandle;
        unsigned currentPage;
        unsigned currentKeyIndex;
        unsigned currentRIDIndex;
        Attribute attribute;
    };

    class IXFileHandle {
    public:

        // variables to keep counter for each operation
        unsigned ixReadPageCounter;
        unsigned ixWritePageCounter;
        unsigned ixAppendPageCounter;
        bool initialized = false;

        // associated fileHandle
        FileHandle *PFHandle;
        bool fileHandleSet = false;

        // Constructor
        IXFileHandle();

        // Destructor
        ~IXFileHandle();

        RC associateFileHandle(FileHandle &fileHandle);

        // Put the current counter values of associated PF FileHandles into variables
        RC collectCounterValues(unsigned &readPageCount, unsigned &writePageCount, unsigned &appendPageCount);

    };
}// namespace PeterDB
#endif // _ix_h_

#include <utility>

#include "../include/pfm.h"
#include <cstring>
#include <string>
#include <cstdlib>
#include <cmath>

namespace PeterDB {
    PagedFileManager &PagedFileManager::instance() {
        static PagedFileManager _pf_manager = PagedFileManager();
        return _pf_manager;
    }

    PagedFileManager::PagedFileManager() = default;

    PagedFileManager::~PagedFileManager() = default;

    PagedFileManager::PagedFileManager(const PagedFileManager &) = default;

    PagedFileManager &PagedFileManager::operator=(const PagedFileManager &) = default;

    RC PagedFileManager::createFile(const std::string &fileName) {
        // Check if file exists, if not then create file
        std::ifstream fileCheck(fileName);
        if (fileCheck.is_open()) {
            fileCheck.close();
            return -1;
        }
        std::ofstream file(fileName);
        if (!file.is_open()) {
            return -1;
        }
        file.close();
        return 0;
    }

    RC PagedFileManager::destroyFile(const std::string &fileName) {
        // Check if file exists, if so then remove it
        std::ifstream fileCheck(fileName);
        if (!fileCheck.is_open()) {
            fileCheck.close();
            return -1;
        }
        remove(fileName.c_str());
        return 0;
    }

    RC PagedFileManager::openFile(const std::string &fileName, FileHandle &fileHandle) {
        // Check that fileHandle is not in use
        if (!fileHandle.fileName.empty()) {
            return -1;
        }
        // Check that file can be opened
        fileHandle.setFileName(fileName);
        std::fstream file(fileName, std::ios::in | std::ios::out | std::ios::binary);
        if (!file.good()) {
            return -1;
        }
        // Check if the file has less than one full page (no metadata page exists)
        file.seekg(0, std::ios::end);
        unsigned fileSize = file.tellg();
        if (fileSize < PAGE_SIZE) {
            // Initialize empty page
            char emptyPage[PAGE_SIZE] = {0};
            file.seekp(0, std::ios::beg);
            file.write(emptyPage, PAGE_SIZE);
            file.seekp(0, std::ios::beg);
            // Write the read, write, and append counters to the newly created page
            int readPageCounter = 0;
            int writePageCounter = 0;
            int appendPageCounter = 0;
            file.write(reinterpret_cast<char*>(&readPageCounter), sizeof(readPageCounter));
            file.write(reinterpret_cast<char*>(&writePageCounter), sizeof(writePageCounter));
            file.write(reinterpret_cast<char*>(&appendPageCounter), sizeof(appendPageCounter));
        } else {
            // File already has a metadata page, read counters from it and update file handler counters
            file.seekg(0, std::ios::beg);
            unsigned readPageCounter = 0, writePageCounter = 0, appendPageCounter = 0;
            file.read(reinterpret_cast<char*>(&readPageCounter), sizeof(readPageCounter));
            file.read(reinterpret_cast<char*>(&writePageCounter), sizeof(writePageCounter));
            file.read(reinterpret_cast<char*>(&appendPageCounter), sizeof(appendPageCounter));
            fileHandle.readPageCounter = readPageCounter;
            fileHandle.writePageCounter = writePageCounter;
            fileHandle.appendPageCounter = appendPageCounter;
        }

        file.close();
        return 0;
    }


    RC PagedFileManager::closeFile(FileHandle &fileHandle) {
        // Ensure all counters are flushed to the file
        fileHandle.updateFileCounters();

        // Reset the file handle's metadata/counters
        fileHandle.readPageCounter = 0;
        fileHandle.writePageCounter = 0;
        fileHandle.appendPageCounter = 0;
        fileHandle.setFileName("");

        return 0;
    }


    FileHandle::FileHandle() {
        readPageCounter = 0;
        writePageCounter = 0;
        appendPageCounter = 0;
    }

    // Extra constructor
    FileHandle::FileHandle(std::string newFileName) {
        readPageCounter = 0;
        writePageCounter = 0;
        appendPageCounter = 0;
        this->fileName = std::move(newFileName);
    }

    FileHandle::~FileHandle() = default;

    RC FileHandle::setFileName(std::string newFileName) {
        // For associating fileHandle with file
        this->fileName = newFileName;
        return 0;
    }

    RC FileHandle::readPage(PageNum pageNum, void *data) {
        unsigned offset = PAGE_SIZE * pageNum + PAGE_SIZE; // Reserve space for first page
        std::ifstream file(this->fileName, std::ios::binary);
        if (!file.is_open()) {
            return -1;
        }

        // Check page number is valid (remember exclude first metadata page)
        unsigned totalPages = getNumberOfPages();
        if (pageNum >= (totalPages)) {
            file.close();
            return -1;
        }

        file.seekg(offset, std::ios::beg); // Go to offset in file
        file.read(reinterpret_cast<char*>(data), PAGE_SIZE);
        file.close();
        readPageCounter++;
        updateFileCounters();
        return 0;
    }

    RC FileHandle::writePage(PageNum pageNum, const void *data) {
        // Open the file for reading and writing
        std::ofstream file(this->fileName, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) {
            return -1;
        }

        // Check page number is valid (remember exclude first metadata page)
        unsigned totalPages = getNumberOfPages();
        if (pageNum >= (totalPages)) {
            file.close();
            return -1;
        }

        // Calculate the offset for the requested page
        unsigned offset = PAGE_SIZE * pageNum + PAGE_SIZE;

        // Seek to the correct position in the file and write the data
        file.seekp(offset, std::ios::beg);
        file.write(reinterpret_cast<const char*>(data), PAGE_SIZE);

        file.close();
        writePageCounter++;
        updateFileCounters();
        return 0;
    }

    RC FileHandle::appendPage(const void *data) {
        // Write data to new page
        std::ofstream file(this->fileName, std::ios::binary | std::ios::app);
        if (!file.is_open()) {
            return -1;
        }
        file.write(reinterpret_cast<const char*>(data), PAGE_SIZE);
        file.close();
        appendPageCounter++;
        updateFileCounters();
        return 0;
    }


    unsigned FileHandle::getNumberOfPages() {
        // Go to end of file, get position, then perform arithmetic to get number of pages not including first hidden page
        std::ifstream file(this->fileName, std::ios::binary);
        file.seekg(0, std::ios::end);
        std::streampos fileSize = file.tellg();
        unsigned numPages = static_cast<unsigned>(fileSize) / PAGE_SIZE;
        numPages--;
        file.close();
        return numPages;
    }

    RC FileHandle::collectCounterValues(unsigned &readPageCount, unsigned &writePageCount, unsigned &appendPageCount) {
        readPageCount = this->readPageCounter;
        writePageCount = this->writePageCounter;
        appendPageCount = this->appendPageCounter;
        return 0;
    }

    RC FileHandle::updateFileCounters() {
        // I will need to add fileHandler's counters to the physical ones on the disk
        unsigned readPageCount, writePageCount, appendPageCount;
        collectCounterValues(readPageCount, writePageCount, appendPageCount);

        std::fstream file(fileName, std::ios::binary | std::ios::in | std::ios::out);

        // Read the current counters from the beginning of the file
        unsigned currentReadPageCount = 0, currentWritePageCount = 0, currentAppendPageCount = 0;
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(&currentReadPageCount), sizeof(currentReadPageCount));
        file.read(reinterpret_cast<char*>(&currentWritePageCount), sizeof(currentWritePageCount));
        file.read(reinterpret_cast<char*>(&currentAppendPageCount), sizeof(currentAppendPageCount));

        // Add the counters from FileHandle to the existing counters
        currentReadPageCount += readPageCount;
        currentWritePageCount += writePageCount;
        currentAppendPageCount += appendPageCount;

        // Write the updated counters back to the file
        file.seekp(0, std::ios::beg);
        file.write(reinterpret_cast<char*>(&currentReadPageCount), sizeof(currentReadPageCount));
        file.write(reinterpret_cast<char*>(&currentWritePageCount), sizeof(currentWritePageCount));
        file.write(reinterpret_cast<char*>(&currentAppendPageCount), sizeof(currentAppendPageCount));

        file.close();
        return 0;
    }
}
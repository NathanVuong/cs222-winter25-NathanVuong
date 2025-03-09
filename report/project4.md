## Project 4 Report


### 1. Basic information
- Team #:
- Github Repo Link:
- Student 1 UCI NetID:
- Student 1 Name:
- Student 2 UCI NetID (if applicable):
- Student 2 Name (if applicable):


### 2. Catalog information about Index
- Show your catalog information about an index (tables, columns).
  - Indexes are denoted in the catalog by an entry in the Tables table. They are named with the following convention: tableName_attributeName.idx. This allows the program to figure out the purpose of the index without needing to create unneeded entries in the Columns table.



### 3. Filter
- Describe how your filter works (especially, how you check the condition.)
  - The filter class contains an instance of a RBFM_ScanIterator, which we initialize at the start with a unique file name. We begin populating it by first extracting the value of the condition we will be comparing it to. Then, we simply iterate through each entry contained in the iterator passed through the parameters. For each entry, we extract the relevant data, compare it to the conditional value, then decide whether to insert that data into the file managed by the scan iterator (this calls a method built in project 3). 



### 4. Project
- Describe how your project works.
  - The project class also contains an instance of a RBFM_ScanIterator, which we initialize at the start with a unique file name. Given the attributes of the input iterator and the passed desired attributes, we can figure out at which attribute indices we will be extracting data. Then, we iterate through each entry in the passed iterator, using the indices of interest to determine when we will grab data and insert it for the scan iterator.



### 5. Block Nested Loop Join
- Describe how your block nested loop join works (especially, how you manage the given buffers.)
  - The project class also contains an instance of a RBFM_ScanIterator, which we initialize at the start with a unique file name, similar to the filter and project classes. In addition, we initialize another rbfm file to hold entries from the left iterator. We begin by initializing a hash table which will map keys to a vector of RIDs. We iterate through all entries in the left index, writing those entries into the extra rbfm file while also filling in the hash table with the key and returned RID. Then, we iterate through the right iterator. For each entry, we will check the hash table for any key matches, and upon finding a match, we grab the matching entry from the rbfm file using the RID, call a helper to merge the two entries together, and insert that into the file managed by the scan iterator. 



### 6. Index Nested Loop Join
- Describe how your index nested loop join works.
  - My index nested loop join follows an identical strategy to the block nested loop join. Refer to the details of block nested loop join.



### 7. Grace Hash Join (If you have implemented this feature)
- Describe how your grace hash join works (especially, in-memory structure).
  - N/A


### 8. Aggregation
- Describe how your basic aggregation works.
  - The aggregation class contains a float value which will hold the value we will return. We simply iterate through the passed iterator, and depending on what attribute and aggregator operator is passed, we update that float value. In the end, we return it, remembering to format it correctly.


- Describe how your group-based aggregation works. (If you have implemented this feature)
  - N/A



### 9. Implementation Detail
- Have you added your own module or source file (.cc or .h)?
  Clearly list the changes on files and CMakeLists.txt, if any.



- Other implementation details:



### 10. Member contribution (for team of two)
- Explain how you distribute the workload in team.
  - Completed solo.



### 11. Other (optional)
- Freely use this section to tell us about things that are related to the project 4, but not related to the other sections (optional)



- Feedback on the project to help improve the project. (optional)
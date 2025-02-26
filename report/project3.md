## Project 3 Report


### 1. Basic information
 - Team #: 6
 - Github Repo Link: https://github.com/NathanVuong/cs222-winter25-NathanVuong
 - Student 1 UCI NetID: ncvuong
 - Student 1 Name: Nathan Vuong
 - Student 2 UCI NetID (if applicable):
 - Student 2 Name (if applicable):


### 2. Meta-data page in an index file
- Show your meta-data page of an index design if you have any.
  - I have one metadata page at the very beginning of the file following the metadata page set up by the PFM. It is a single page that contains the root page number.



### 3. Index Entry Format
- Show your index entry design (structure). 

  - entries on internal nodes:
    - Each key has the page number whose first ordered entry is less to its left, and the page whose first ordered entry is equal to or greater than (depending on if it is a parent of another internal or a leaf node) to its right.
  
  - entries on leaf nodes:
    - Each entry in order contains the key, a counter for the number of RIDs in the tree, and then a list of the RIDs that have that key.



### 4. Page Format
- Show your internal-page (non-leaf node) design.
  - Internal nodes have 12 bytes allocated at the beginning to store a leaf bit, the number of keys in the page, and the parent of that page (which is 0 if that page is the root). The rest of the page will store the keys and page numbers.



- Show your leaf-page (leaf node) design.
  - Leaf nodes have 12 bytes allocated at the beginning to store a leaf bit, the number of keys in the page, and the parent of that page (which is 0 if that page is the root). Additionally, the last 4 bytes of the page are reserved to contain the page number of the sibling. The rest of the page will contain the keys, RID counter, and RIDs.



### 5. Describe the following operation logic.
- Split
  - When a leaf node becomes full, it is split in half with half the records remaining in the same page, and the other half going to a new page. In the case of odd entries, the new page gets it. The sibling of the original page is given to the new page, and the original page sets the new page as its sibling. The first entry of the new page will have its key copied up as well. In the case of an internal node, the key is moved up instead of copied up. This process is called recursively if needed, and the root is updated if needed. 



- Rotation (if applicable)
  - N/A



- Merge/non-lazy deletion (if applicable)
  - N/A



- Duplicate key span in a page
  - Duplicate keys have their RIDs appended to the current key RIDs.



- Duplicate key span multiple pages (if applicable)
  - N/A



### 6. Implementation Detail
- Have you added your own module or source file (.cc or .h)? 
  Clearly list the changes on files and CMakeLists.txt, if any.



- Other implementation details:



### 7. Member contribution (for team of two)
- Explain how you distribute the workload in team.
  - Completed alone.



### 8. Other (optional)
- Freely use this section to tell us about things that are related to the project 3, but not related to the other sections (optional)



- Feedback on the project to help improve the project. (optional)

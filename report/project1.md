## Project 1 Report


### 1. Basic information
 - Team #: 6
 - Github Repo Link: https://github.com/NathanVuong/cs222-winter25-NathanVuong
 - Student 1 UCI NetID: ncvuong
 - Student 1 Name: Nathan Vuong
 - Student 2 UCI NetID (if applicable):
 - Student 2 Name (if applicable):


### 2. Internal Record Format
- Show your record format design.
  - Records will appear with a null byte array followed by the data. VarChars are handled a bit differently and will be explained below.



- Describe how you store a null field.
    - In the header of a record, the start contains a byte array which has information regarding the null values. It stores a 1 if null, and a 0 otherwise.


- Describe how you store a VarChar field.
  - A VarChar will have 4 bytes reserved at the beginning to store the length before storing the actual content.



- Describe how your record design satisfies O(1) field access.
  - When given an RID a user can easily locate the page and find the record offset through the slot directory. Then, they can use the attributes to easily jump to their desired field through pointer arithmetic. All of these processes take no longer than O(1) time.



### 3. Page Format
- Show your page format design.
  - With the exception of the first "hidden" metadata page, all pages follow the same format. Records are stored at the start of the page, and a slot directory which grows from right to left is stored at the very end of the page. This leaves the free space in the middle of the page for new records to slide in.



- Explain your slot directory design if applicable.
  - My slot directory is stored at the end of each record page. From right to left, it consists of the number of slots used, a pointer to free space, and the slots which themselves consist of the offset of the record from the start of the page and the size of the record.



### 4. Page Management
- Show your algorithm of finding next available-space page when inserting a record.
  - As requested by the Canvas assignment, my RBFM first checks the last page to see if there is space to insert the record and adjust the slot directory. If not, it will check from the first page and continue until before the last page to see if it can find a suitable page. If not, it will finally append a brand new page.



- How many hidden pages are utilized in your design?
  - One, just the first page is being used to store metadata for PFM such as an append and writing counter.



- Show your hidden page(s) format design if applicable
  - From left to right, I just simply store the read, write, and append counters.



### 5. Implementation Detail
- Other implementation details goes here.



### 6. Member contribution (for team of two)
- Explain how you distribute the workload in team.
  - N/A



### 7. Other (optional)
- Freely use this section to tell us about things that are related to the project 1, but not related to the other sections (optional)



- Feedback on the project to help improve the project. (optional)
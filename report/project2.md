## Project 2 Report


### 1. Basic information
 - Team #: 6
 - Github Repo Link: https://github.com/NathanVuong/cs222-winter25-NathanVuong
 - Student 1 UCI NetID: ncvuong
 - Student 1 Name: Nathan Vuong
 - Student 2 UCI NetID (if applicable):
 - Student 2 Name (if applicable):

### 2. Meta-data
- Show your meta-data design (Tables and Columns table) and information about each column.
  - The Tables and Columns tables are created using methods implemented in the RBFM. 
  - Tables have records with the attributes table-id, table-name, and file-name. These represent the tables that have been created, table-name and file-name will be the same.
  - Columns have records with the attributes table-id, column-name, column-type, column-length, and column-position. These records represent the attributes of each table.


### 3. Internal Record Format (in case you have changed from P1, please re-enter here)
- Show your record format design.



- Describe how you store a null field.



- Describe how you store a VarChar field.



- Describe how your record design satisfies O(1) field access.



### 4. Page Format (in case you have changed from P1, please re-enter here)
- Show your page format design.



- Explain your slot directory design if applicable.
  - Deleted records will be denoted with a length and offset of 0.
  - Tombstones will be denoted with their offset and a length of -1.


### 5. Page Management (in case you have changed from P1, please re-enter here)
- How many hidden pages are utilized in your design?



- Show your hidden page(s) format design if applicable



### 6. Describe the following operation logic.
- Delete a record
  - When deleting a record, that record's slot directory entry is set to have a length and offset of 0, indicating a deleted record and a reusable slot directory. When a record is deleted, all records to the right of the deleted record will be moved left to fill in the space, and the slot directory will be updated accordingly for those records. We must also consider that the passed RID may point to a series of tombstones, in which they must all be handled in the same manner.


- Update a record
  - When updating a record, there are a few scenarios that need to be handled. If the updated record is the same size as the old record, we can simply update in place. If it is smaller, records to its right must be moved left to fill the gap and the slot directory must be updated to reflect a new record length and new offsets for the shifted records. If it is bigger than the old record but can still fit on the same page, records to its right will all be moved right to make space, and of course the slot directory will be updated. If it is too big to fit on the page, InsertRecord will be called and we will set up a tombstone with the returned RID, and we will modify the slot directory. The entry for the tombstone in the slot directory will contain the offset of the tombstone, and a length of -1.



- Scan on normal records
  - When scanning normal records, the RBFM iterates through all slots and pages and finds the attribute to be compared. It then makes that comparison before adding it to the iterator. The iterator itself uses the RBFM to create a file with the filename table_scan and stores all the scanned records there.



- Scan on deleted records
  - Deleted records are denoted with a record offset and length of 0 and are not scanned.


- Scan on updated records
  - Tombstones are ignored since the record they point to will be reached eventually and we are not concerned with the original RID right now.



### 7. Implementation Detail
- Other implementation details goes here.



### 8. Member contribution (for team of two)
- Explain how you distribute the workload in team.
  - All me.


### 9. Other (optional)
- Freely use this section to tell us about things that are related to the project 1, but not related to the other sections (optional)
  - Instead of have the RBFM check every file from the start for a place to insert a record, I will now have it count backwards by 20. This is due to excessive search time and wasted checking on files which are already full or very unlikely to hold it.



- Feedback on the project to help improve the project. (optional)
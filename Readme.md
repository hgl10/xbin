# A Simple Binary Virtual Table for SQLite3

## Current Status

- where row = ?
  need manual limit 1
- insert
  append data to eof

## Usage

```sqlite
.load xbin
create virtual table xbin using xbin(./test.bin);
select count(*) from xbin;
```

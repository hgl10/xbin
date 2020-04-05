# A  Virtual Table for SQLite3

## Usage

```sqlite
.load xbin
create virtual table xbin using xbin(./test.bin);
select * from xbin;
```


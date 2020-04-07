.load ./xbin
create virtual table xbin using xbin(./test.bin);
.timer on
.mode column
.header on
delete from xbin where rowid = 1;
insert into xbin(id, iq) values (1,2);

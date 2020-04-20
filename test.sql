.load ./xbin
create virtual table xbin using xbin(./test.bin);
.timer on
.mode column
.header on
.echo on
-- .eqp trace
-- .wheretrace
-- select rowid, * from xbin where rowid > 1000000 order by rowid limit 10;
-- delete from xbin where rowid = 1;
-- insert into xbin(id, iq) values (1,2);
-- update xbin set id = 15 where rowid = 100000;
-- explain select rowid, * from xbin where row = 4;
select rowid, * from xbin where row >= 5000000 limit 5;

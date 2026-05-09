# SQLITE3
**Name:** Nitish | **Batch:** A | **Roll:** 24BCS10589 | **Lab:** 02 | **Title:** Storage_Engine_2

> I (Nitish) did these things in sqlite3. And, used the commands which sir taught in the lecture.

---

* first created the db, with table users, the size was 8.0K

  ```
  -rwxr-xr-x 1 zephoryx root 8.0K May  9 12:10 sample.db
  ```

  we can see that it exists as a normal file in the system. This was interesting because internally the whole database is basically being stored inside this single file only.

---

* then added 10,000 rows and size became 196K

  ```
  -rwxr-xr-x 1 zephoryx root 196K May  9 12:10 sample.db
  ```

  After adding more data, the database file size increased automatically. This helped me understand that the database keeps storing new data inside the same file by allocating more pages internally.

---

* `PRAGMA page_size` returned 4096, meaning the page size is 4KB.

  This means SQLite divides the database into fixed-size pages of 4KB each. Instead of handling data byte-by-byte, it works page-by-page internally.

---

* `PRAGMA page_count` returned 49, so it has 49 pages after adding 10,000 rows, and, we can see that total_size/page_size, 196/4, is equal to 49 only!

  This was very interesting because it matched almost perfectly with the actual database size. It helped me understand that the entire database file is internally organized into pages.

---

* `PRAGMA mmap_size` returned 0, currently it is disabled.

  This means mmap was not being used initially.

---

* Now I set the mmap_size to 30MB, so it can use the mmap area.

  mmap basically allows SQLite to map the database file directly into memory instead of doing extra copying again and again.

---

* I resetted the mmap to 0, Then I `time` the `select * from users;` query and this was the result : -

  ```
  real    0m0.070s
  user    0m0.014s
  sys     0m0.023s
  ```

---

* Now I tried it with assigning mmap_size to 30MB, these were the results : -

  ```
  real    0m0.091s
  user    0m0.006s
  sys     0m0.040s
  ```

  Surprisingly mmap was not faster here. I learned that mmap is not always guaranteed to improve performance. Since the dataset was small and Linux already uses page cache internally, the difference was very small.

---

* I ran `sqlite3 sample.db` in another terminal then checked its process in another : -

  ```
  zephoryx   47121  0.0  0.0  12056  5424 pts/0    S+   16:47   0:00 sqlite3 sample.db
  zephoryx   47693  0.0  0.0   9156  2296 pts/2    S+   16:49   0:00 grep --color=auto sqlite
  ```

  After exiting it, the process was not shown : -

  ```
  zephoryx   47907  0.0  0.0   9156  2300 pts/2    S+   16:49   0:00 grep --color=auto sqlite
  ```

  This helped me understand that SQLite is an embedded database. It does not run a permanent database server in the background. Once the sqlite3 program exits, the process is completely gone.

---

* then to observe syscalls I did `strace sqlite3 sample.db` and there were many things in the output, I was unable to understand most of them, I'm attaching a small piece of output : -

  ```
  brk(NULL)                               = 0x59af8b385000
  mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x77da2d28e000
  access("/etc/ld.so.preload", R_OK)      = -1 ENOENT (No such file or directory)
  openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
  fstat(3, {st_mode=S_IFREG|0644, st_size=81467, ...}) = 0
  mmap(NULL, 81467, PROT_READ, MAP_PRIVATE, 3, 0) = 0x77da2d27a000
  close(3)                                = 0
  openat(AT_FDCWD, "/lib/x86_64-linux-gnu/libsqlite3.so.0", O_RDONLY|O_CLOEXEC) = 3
  read(3, "\177ELF\2\1\1\0\0\0\0\0\0\0\0\0\3\0>\0\1\0\0\0\0\0\0\0\0\0\0\0"..., 832) = 832
  fstat(3, {st_mode=S_IFREG|0644, st_size=1468440, ...}) = 0
  mmap(NULL, 1472056, PROT_READ, MAP_PRIVATE|MAP_DENYWRITE, 3, 0) = 0x77da2d112000
  mmap(0x77da2d132000, 1093632, PROT_READ|PROT_EXEC, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x20000) = 0x77da2d132000
  ```

  Even though I could not understand every syscall, I was able to notice important things like:

  * `openat()`
  * `read()`
  * `mmap()`
  * `close()`

  This showed me that even opening SQLite internally involves many low-level OS operations and system calls.

---

* Then I checked the inode number of sample.db : -

  ```
  104371 sample.db
  ```

  This inode number acts like an internal identity of the file inside Linux.

---

* As we know that sqlite3 is an EMBEDDED DB, so creating another table won't create another file.

  ran this command : -

  ```
  CREATE TABLE products (
  id INTEGER PRIMARY KEY,
  price INT
  );
  ```

  inserted few rows, then checked if a new file is created or not.

  ```
  -rwxr-xr-x 1 zephoryx root 200K May  9 17:17 sample.db
  ```

  No new file is created, the size of sample.db increased by 4KB only, and the inode number is same as before : -

  ```
  104371 sample.db
  ```

  This helped me understand that SQLite stores multiple tables inside the same database file itself instead of creating separate files for every table.

---

# POSTGRESQL

> Now it's time for PSQL.

---

* Did the same as I did in sqlite3, created a db and a users table, then added 10,000 rows.

---

* used `\timing` to turn on the timing of queries.

---

* Then I did `select * from users;` and these were the results : -

  ```
  Time: 4.193 ms
  ```

  PostgreSQL query execution was very fast even with large data.

---

* did `SHOW block_size;` and it returned 8192, so the block size is 8KB.

  This means PostgreSQL internally stores data in 8KB pages/blocks.

---

* to calc the page size in psql did this, it is more tricky than sqlite3 because PostgreSQL internally manages storage differently : -

  ```
  SELECT pg_relation_size('users') / 8192 AS approx_pages;
  ```

  and the result was : -

  ```
   approx_pages 
  --------------
              64
  ```

  This helped me estimate how many internal pages the table is occupying.

---

* to observe memory related buffering, I did `SHOW shared_buffers;` and it returned 128MB.

  PostgreSQL internally keeps a shared memory buffer for caching pages in RAM. Unlike SQLite, it does not expose mmap_size in the same simple way.

---

* Just like sqlite3, I tried to see if creating another table creates another file or not.

  I first checked the directory location of the users table : -

  ```
  SELECT pg_relation_filepath('users');
  ```

  and it returned :

  ```
  base/16388/16390
  ```

  then I created another table products and checked its location : -

  ```
  SELECT pg_relation_filepath('products');
  ```

  and it returned :

  ```
  base/16388/16399
  ```

  so we can see that both tables are in the same directory, but they are different files.

  This is very different from SQLite. PostgreSQL stores tables separately internally, while SQLite stores everything inside one database file.

---

* Now to check if the psql process is there before and after closing like sqlite3 or not.

  I ran `ps aux | grep postgres` and these were the results : -

  ```
  postgres    2175  0.0  0.1 225756 23812 ?        Ss   12:25   0:00 postgres: 16/main: checkpointer 
  postgres    2176  0.0  0.0 225628  7808 ?        Ss   12:25   0:00 postgres: 16/main: background writer 
  postgres    2182  0.0  0.0 225476 10348 ?        Ss   12:25   0:00 postgres: 16/main: walwriter 
  postgres    2183  0.0  0.0 227080  8520 ?        Ss   12:25   0:00 postgres: 16/main: autovacuum launcher 
  postgres    2184  0.0  0.0 227056  7976 ?        Ss   12:25   0:00 postgres: 16/main: logical replication launcher 
  root       58805  0.0  0.0  19860  7816 pts/2    S+   17:24   0:00 sudo -u postgres psql
  root       58825  0.0  0.0  19860  2716 pts/3    Ss   17:25   0:00 sudo -u postgres psql
  postgres   58826  0.0  0.0  26096  9296 pts/3    S+   17:25   0:00 /usr/lib/postgresql/16/bin/psql
  postgres   59796  0.0  0.1 228520 22944 ?        Ss   17:28   0:00 postgres: 16/main: postgres labdb [local] idle
  zephoryx   61282  0.0  0.0   9156  2292 pts/0    S+   17:32   0:00 grep --color=auto postgres
  ```

  After exiting psql using `\q`, the PostgreSQL related processes were still running : -

  ```
  postgres    2164  0.0  0.1 225476 30272 ?        Ss   12:25   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
  postgres    2175  0.0  0.1 225756 23812 ?        Ss   12:25   0:00 postgres: 16/main: checkpointer 
  postgres    2176  0.0  0.0 225628  7808 ?        Ss   12:25   0:00 postgres: 16/main: background writer 
  postgres    2182  0.0  0.0 225476 10348 ?        Ss   12:25   0:00 postgres: 16/main: walwriter 
  postgres    2183  0.0  0.0 227080  8520 ?        Ss   12:25   0:00 postgres: 16/main: autovacuum launcher 
  postgres    2184  0.0  0.0 227056  7976 ?        Ss   12:25   0:00 postgres: 16/main: logical replication launcher 
  zephoryx   61659  0.0  0.0   9156  2300 pts/0    S+   17:34   0:00 grep --color=auto postgres
  ```

  This helped me understand that PostgreSQL works very differently from SQLite. It runs as a proper database server in the background with multiple helper/background processes running continuously.

---

# FINAL THINGS I LEARNED

* Databases are ultimately files stored in the system.
* Data is internally divided into fixed-size pages.
* SQLite is lightweight and embedded.
* PostgreSQL is heavier but much more powerful internally.
* mmap maps database files into memory.
* Even simple database operations involve many low-level system calls.
* SQLite stores multiple tables inside one database file.
* PostgreSQL stores tables separately internally.
* PostgreSQL continues running in background even after exiting psql.
* Storage Engines are deeply connected with Operating Systems and memory management.
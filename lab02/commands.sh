#!/bin/bash
# ============================================================================
# Name: Nitish
# Batch: A
# Roll: 24BCS10589
# Lab: 02
# Title: Storage_Engine_2
# ============================================================================

echo "========================================="
echo "        SQLITE3 EXPERIMENTS"
echo "========================================="

echo
echo "1. Checking database file size"
ls -lh sample.db

echo
echo "2. Checking SQLite page size"
sqlite3 sample.db "PRAGMA page_size;"

echo
echo "3. Checking SQLite page count"
sqlite3 sample.db "PRAGMA page_count;"

echo
echo "4. Checking current mmap size"
sqlite3 sample.db "PRAGMA mmap_size;"

echo
echo "5. Setting mmap_size to 30MB"
sqlite3 sample.db "PRAGMA mmap_size = 30000000;"

echo
echo "6. Verifying mmap size"
sqlite3 sample.db "PRAGMA mmap_size;"

echo
echo "7. Timing SELECT query"
time sqlite3 sample.db "SELECT * FROM users;" > /dev/null

echo
echo "8. Checking inode number"
ls -i sample.db

echo
echo "9. Checking sqlite3 process"
ps aux | grep sqlite

echo
echo "========================================="
echo "      POSTGRESQL EXPERIMENTS"
echo "========================================="

echo
echo "1. Checking PostgreSQL block size"
sudo -u postgres psql -d labdb -c "SHOW block_size;"

echo
echo "2. Checking approximate page count"
sudo -u postgres psql -d labdb -c \
"SELECT pg_relation_size('users') / 8192 AS approx_pages;"

echo
echo "3. Checking shared buffers"
sudo -u postgres psql -d labdb -c "SHOW shared_buffers;"

echo
echo "4. Timing SELECT query"
sudo -u postgres psql -d labdb -c "\\timing on" \
-c "SELECT * FROM users;"

echo
echo "5. Checking users table file path"
sudo -u postgres psql -d labdb -c \
"SELECT pg_relation_filepath('users');"

echo
echo "6. Checking products table file path"
sudo -u postgres psql -d labdb -c \
"SELECT pg_relation_filepath('products');"

echo
echo "7. Checking PostgreSQL processes"
ps aux | grep postgres

echo
echo "========================================="
echo "         EXPERIMENTS COMPLETE"
echo "========================================="
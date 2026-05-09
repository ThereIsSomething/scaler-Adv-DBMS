# RAW SYSTEM CALLS LAB
**Name:** Nitish | **Batch:** A | **Roll:** 24BCS10589 | **Lab:** 01 | **Title:** Storage_Engine

> In this lab I (Nitish) explored low level Linux file handling using raw system calls in C++.
> Instead of using high level things like `ifstream` or `ofstream`, I directly used Linux sys calls like:
>
> * `open()`
> * `write()`
> * `read()`
> * `lseek()`
> * `close()`
>
> This helped me understand how file handling actually works internally between our program and the OS.

---

# COMPILATION

```bash
g++ main.cpp -std=c++17 -o app
```

* `-std=c++17` sets the C++ version.
* `-o app` gives custom name to the output executable.

---

# 1. OPEN FILE

```cpp
int fd = open(
    "nitish-det.txt",
    O_RDWR | O_CREAT,
    0644
);
```

Here I used `open()` instead of high level file libraries.

---

## Things I learned

* `open()` is a raw Linux syscall.
* It returns a small integer called:

# File Descriptor (FD)

Example:

```text
3
```

---

## What is FD?

FD is basically a representative/handle of the opened file.

Now instead of talking to file directly, we use this FD in further syscalls.

Example:

```cpp
write(fd, ...)
read(fd, ...)
close(fd)
```

---

## Meaning of Flags

```cpp
O_RDWR
```

means:

* open for reading
* open for writing

---

```cpp
O_CREAT
```

means:

* create file if it does not exist

---

## File Permission

```cpp
0644
```

This only matters while creating file.

It means:

* owner can read/write
* others can only read

---

## Important Observation

If opening fails:

```cpp
fd < 0
```

Linux returns `-1`.

---

# 2. WRITE TO FILE

```cpp
write(fd, msg, strlen(msg));
```

This syscall writes bytes into the file.

---

## Why `const char*` instead of string?

I learned that kernel/syscalls work directly with raw memory addresses and bytes.

`std::string` is a high level container.

But:

```cpp
const char*
```

directly gives memory address of characters.

That is why syscall can understand it properly.

---

## Another Observation

I also tested:

```cpp
msg2.c_str()
```

which converts:

```text
string → const char*
```

so that syscall can use it.

---

## Offset Concept

After first `write()`:

```text
file offset moved ahead
```

So second `write()` happened from next position instead of overwriting previous text.

This helped me understand that Linux internally keeps a current position pointer for every opened file.

---

# 3. MOVING FILE OFFSET

```cpp
lseek(fd, 0, SEEK_SET);
```

This syscall manually changes current file position.

---

## Meaning

```cpp
SEEK_SET
```

means:

* start counting from beginning of file

---

So this line:

```cpp
lseek(fd, 0, SEEK_SET);
```

basically moved offset back to starting of file.

Now next `read()` or `write()` starts from beginning again.

---

# 4. READ FILE

```cpp
read(fd, buffer, sizeof(buffer)-1);
```

This syscall reads bytes from file into memory.

---

## Buffer Concept

```cpp
char buffer[1024];
```

I gave OS a memory area where upcoming file data can be stored.

---

## Why `char[]`?

Because syscalls work with:

* raw bytes
* memory addresses

instead of high level strings.

---

## Null Terminator

```cpp
buffer[bytesRead] = '\0';
```

This was needed so that:

```cpp
cout << buffer
```

knows where actual content ends.

Otherwise garbage values may print.

---

# 5. CLOSE FILE

```cpp
close(fd);
```

This closes the opened file descriptor.

---

## Why Important?

Closing FD releases resources used by kernel.

If not closed properly:

* resources remain occupied
* memory/resources may leak

---

# FILE DESCRIPTORS

One of the biggest things I understood from this lab was:

# Linux treats files using File Descriptors.

---

## Internal Simplified Flow

```text
Process
   ↓
File Descriptor
   ↓
Kernel
   ↓
inode
   ↓
Actual File Data
```

---

## Important Point

FD is NOT the file itself.

It is just:

* an integer handle
* through which kernel identifies opened file

---

# INODE

I also learned about inode.

inode is like internal identity of a file inside Linux.

It stores:

* file size
* permissions
* timestamps
* disk block info

instead of filename itself.

---

## Checking inode

Command:

```bash
ls -i nitish-det.txt
```

Example:

```text
104371 nitish-det.txt
```

Here:

```text
104371
```

is inode number.

---

# STRACE

I used:

```bash
strace ./app
```

to observe syscalls happening internally.

---

## Some syscalls I noticed

```text
openat()
read()
write()
close()
mmap()
```

Even though I could not understand every line fully, it showed me that many low level OS operations happen internally even for simple programs.

---

# WHAT I UNDERSTOOD FROM THIS LAB

* High level file libraries internally depend on system calls.
* Linux communicates with files using File Descriptors.
* Syscalls directly interact with kernel.
* OS internally keeps track of current file offset.
* Files internally have inode identities.
* Reading/writing files actually involves kernel level operations.
* Even simple file handling is deeply connected with Operating Systems.

---

# FINAL THOUGHT

Before this lab I used to think file handling is just:

```cpp
fstream file;
```

But now I understood that internally many low level things happen like:

* syscalls
* kernel interaction
* offsets
* inode lookup
* memory buffers
* file descriptors

This lab gave me a much deeper understanding of how Operating Systems and storage systems actually work together.
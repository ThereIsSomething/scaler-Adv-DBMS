/*
 * ============================================================================
 * Name: Nitish
 * Batch: A
 * Roll: 24BCS10589
 * Lab: 01
 * Title: Storage_Engine
 * ============================================================================
 */

// #include <bits/stdc++.h> // iss se bas standard libraries aati, but apan ko
// POSIX standard libs chaiye.

#include <cstring>
#include <fcntl.h> // open()
#include <iostream>
#include <sys/stat.h>
#include <unistd.h> // read(), write(), close()

using namespace std;
#define endl                                                                   \
  "\n" // taki baar merko \n naa likhna pade, endl muscle memory me hai :)

int main() {
  // ==============
  // 1. OPEN FILE
  // ==============
  int fd = open( // limited int num for fd w/ diff meaning
      "nitish-det.txt",
      O_RDWR | O_CREAT | O_APPEND, // bit flags to control how file is opened, | to combine
                        // the bits
      0644 // file permision,only jab file create kar rahe ho toh, 0 for octal
  );

  if (fd < 0) // fail condition me OS -1 return karta hai
  {
    cerr << "Failed to open file" << endl; // cerr : error throw karne ke liye
    return -1;
  }

  cout << "Nitish's File Descriptor : " << fd << endl;

  // compile : g++ main.cpp -std=c++17 -o app
  // -std flag to set the version, -o to set the custom name of the output file

  //===================
  // 2. WRITE TO FILE
  //===================
  const char *msg = "Nitish doing Raw Linux Sys Calls!\n";
  // why not string? cuz string is a proper "container" which kernel can't
  // understand, const char* ek pointer hai jo direct uss string ko point kar
  // rha hai in the memory, and kernel exactly wants this address. write() ek
  // syscall hai toh uske ander waise bhi string nahi jaa payegi if you still
  // wanna send a string then you can send as msg.c_str(), it will do string ->
  // const char*

  //
  ssize_t bytesWritten =
      write( // ye ander ki data ko overwrite kardega as current offset
             // (starting point) 0 (start) hoga
          fd, msg,
          strlen(msg) // kinda kitne bytes hai wo return karta hai but uss
                      // datatype? me jo write() ko chaiye, to tell kernel kitne
                      // bytes likhne hai
      );
  // abhi offset change ho gaya hoga, if you will do again write then wo aage se
  // hoga.

  if (bytesWritten < 0) // this was the exact reason why we wrote "ssize_t",
                        // iska matlab hai signed size type and hence it can
                        // return both positive val and neg when failure
  {
    cerr << "Nahi likh paya" << endl;
    close(fd);
    return -1;
  }

  cout << "Bytes Written : " << bytesWritten << endl;

  string msg2 = "Nitish ka ye msg kidhar hoga ?";

  ssize_t bytesAnother = write(fd, msg2.c_str(), strlen(msg2.c_str()));

  if (bytesAnother < 0) {
    cerr << "Khatam" << endl;
    close(fd);
    return -1;
  }

  cout << "Wapas se itna likha : " << bytesAnother << endl;
  // dekha, line 2 pe tha apna new data, line 2 due to "\n" in the prev write
  // ops.

  // ==========================
  // 3. Moving the file offset
  // =========================

  lseek(fd,      // konse file ka change karna hai
        0,       // kitna
        SEEK_SET // kaha se
  );
  // it will move 0 from starting(SEEK_SET) for our "fd" => starting offset, les
  // go

  // ab write ya read karke dekhlo starting se hoga.

  // =============
  // 4. READ FILE
  // =============

  char buffer[1024];
  // char array as read() works with bytes and not strings
  // so apan ne OS ko ek memory area dedi upcoming data store karne ke liye

  ssize_t bytesRead =
      read(fd,                // apna fd
           buffer,            // kidhar store karna hai
           sizeof(buffer) - 1 // max bytes to store, -1 as apan ne ek byte null
                              // terminator ke liye reserve kii hai
      );

  if (bytesRead < 0) {
    cerr << "khatam" << endl;
    close(fd);
    return -1;
  }

  buffer[bytesRead] = '\0'; // null terminator, well ye cout ko inform karne ke
                            // liye hai kii yanhi tak print karna otherwise you
                            // will se some garbage values being printed.

  cout << "\nNitish's Content Read from File: " << endl;
  cout << buffer << endl; // the benefit of char array, you can directly print
                          // it's whole contents like this

  // =============
  // 5. CLOSE FILE
  // =============

  close(fd); // closing the fd is necessary to stop memory from being leaked

  return 411;
}
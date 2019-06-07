# F5 Archiver
This repository contains a description and C implementation of a steganographic data compression technique based on the [F5 algorithm](https://link.springer.com/chapter/10.1007%2F3-540-45496-9_21) in forms of a library and a command-line tool.
With it, you can embed any binary data in your JPEG files reducing total data space in your memory without noticeable changes in used images.

You can read a little more on the subject in my [medium post](https://medium.com/@labunskya/about-a-strange-data-compression-method-4d0d9d2e5714).

## Build
Simple `make` used to build everything. If you want to follow a non-Unix way, you'll need to figure it out for yourself.

### Dependencies
This project depends on [libjpeg](http://libjpeg.sourceforge.net) (for JPEG decoding and encoding), [pcreposix](https://www.pcre.org) (regular expressions support) and [tinydir](https://github.com/cxong/tinydir) (for a platform-independent filesystem traversal) APIs.
Since tinydir provided via the simple header file included in the tree, you only need to make sure you have POSIX regex libjpeg-compatible APIs linked during the compilation.
If it is not present on your machine, you can build static versions of [libjpeg-turbo's](https://libjpeg-turbo.org) and [pcre] from an official repositories locally using `make libjpeg` command.
All necessary headers are also included in the tree. 

## Usage
You can use F5-based data compression via both command-line tool and C/C++ API.

### Command-line tool
You pack and unpack archives from a terminal using f5ar executable in a simple Unix way.

Locate built executable `f5ar` and use this command to pack data:
~~~
./f5ar -p [root library folder] [regex] [file to compress] [archive name]
~~~
And this one to unpack it: 
~~~bash
./f5ar -u [acrhive file path] [output file]
~~~

### API
An API provided by the target `libf5ar` and `f5ar.h` header file. 

Typical packing process looks like this:

1. Allocate `f5archive` (on a stack or on a heap) and fill it with zeroes
2. Initialize it with `f5ar_init()` call
3. Call `f5ar_add*()` functions to add JPEG files and form desired archive
4. (optional) Use `f5ar_analyze()` to check if you have enough capacity in your library
5. Do `f5ar_pack()` with your data
6. Save your archive by serializing `meta` field and order exported with `f5ar_export_order()` 

Typical unpacking process flow:

1. Allocate `f5archive` (on a stack or on a heap) and fill it with zeroes
2. Initialize it with `f5ar_init()` call
3. Deserialize `meta` field and order, import the last one with `f5ar_import_order()`
4. Call `f5ar_fill*()` functions to fill archive with your JPEG files until the returned value is `F5AR_OK_COMPLETE`
5. Do `f5ar_unpack()` and retrieve your data

You can use [utility source code](f5ar_cmd.c) as an example if you need more info.
Also check out [the main header file](f5ar.h) for more insight on advanced usage. 

### Command-line-like API
If you are a weirdo and want to use the library the same way you are using the command-line tool, you can use `libf5arcmd` target and `f5ar_cmd.h` header file avoiding any confusion using process forking, pipes and other stuff. 

## License
[Simplified BSD](LICENSE).

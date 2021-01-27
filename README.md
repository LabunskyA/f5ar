# F5 Archiver
This repository contains a pure C implementation of a steganographic data compression technique based on the [F5 algorithm](https://link.springer.com/chapter/10.1007%2F3-540-45496-9_21) in forms of a library and a command-line tool.
With it you can embed any binary data in your JPEG files while reducing total data space in your memory without noticeable changes in used images.

You can read a little more on the subject in my [medium post](https://medium.com/@labunskya/about-a-strange-data-compression-method-4d0d9d2e5714).

## Building
Simple `make` is used to build everything. On windows you can use Cygwin or WSL to use it properly. If you want to follow a non-Unix way, you'll need to figure it out for yourself.

### Dependencies
This project depends on [libjpeg](http://libjpeg.sourceforge.net) (for JPEG decoding and encoding), [pcre](https://www.pcre.org) and [tinydir](https://github.com/cxong/tinydir) APIs. Since tinydir is provided via the simple header file included in the tree, you only need to make sure you have POSIX regex and libjpeg-compatible APIs linked during the compilation.

If something is not present on your machine, you can build static versions of both [libjpeg-turbo's](https://libjpeg-turbo.org) and pcre from an official repositories locally using `make libjpeg` and `make pcre` commands. Note that you will need `wget`, `git` and `cmake`.

All necessary headers included in the tree.

## Usage
You can use F5-based data compression via both a command-line tool and a C/C++ API.

### Command-line tool
Pack and unpack archives from a terminal using f5ar executable in a simple Unix way.

Locate built executable `f5ar` and use this command to pack data:
~~~bash
./f5ar -p [root library folder] [regex] [file to compress] [archive name]
~~~
And this one to unpack it: 
~~~bash
./f5ar -u [acrhive file path] [output file]
~~~

Make sure that your regex matches only actual jpeg files to prevent any kinds of misunderstandings and possibly ruin your data.

### API
An API provided by the target `libf5ar` and `f5ar.h` header file. 

Typical packing process looks like this:

1. Allocate `f5archive` and fill it with zeroes;
2. Initialize it with `f5ar_init()` call;
3. Call `f5ar_add*()` functions to add JPEG files and form a desired archive;
4. (optional) Use `f5ar_analyze()` to check if you have enough capacity in your fresh library;
5. Call `f5ar_pack()` with your data;
6. Save the archive by serializing `meta` field and the order exported with `f5ar_export_order()`.

Typical unpacking process flow:

1. Allocate `f5archive` and fill it with zeroes;
2. Initialize it with `f5ar_init()` call;
3. Deserialize `meta` field and the order, import the last one with `f5ar_import_order()`;
4. Use `f5ar_fill*()` functions to fill the archive with JPEG files until the returned value is `F5AR_OK_COMPLETE`;
5. Call `f5ar_unpack()` and retrieve your data.

You can use [the utility source code](f5ar_cmd.c) as an example if you need more info.
Also check out [the main header file](f5ar.h) for more insights on advanced usage. 

### Command-line-like API
If you are a weirdo and want to use the library the same way you are using the command-line tool, you can use `libf5arcmd` target and `f5ar_cmd.h` header file avoiding any confusion using a process forking, pipes and other stuff. 

## License
[Simplified BSD](LICENSE)

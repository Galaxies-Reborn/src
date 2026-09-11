# Star Wars Galaxies Source Code (C++) Repository

This repository contains the C++ server code maintained by Galaxies Reborn.

## Current development

The primary branch is `main`. Follow the [Galaxies Reborn branch policy](https://github.com/Galaxies-Reborn/galaxies-reborn/blob/main/BRANCHES.md) for the maintained x64 server and feature efforts.

# Building

## Clang Versions

**Important**: For versions of clang <= 4 you'll probably have to remove/omit a deprecated CFLAG or two from the CMakelists.txt file

Only use the Debug and Release targets unless you want to work on 64 bit (MODE=RELWITHDEBINFO). For local testing, and non-live builds set MODE=Release or MODE=debug in build_linux.sh.

For production, user facing builds, set MODE=MINSIZEREL for profile built, heavily optimized versions of the binaries.

## Profiling and Using Profiles (IN-WORK)

To generate new profiles, build SWG with MODE=RELWITHDEBINFO. 

Add export LLVM_PROFILE_FILE="output-%p.profraw" to your startServer.sh file. 

WHILE THE SERVER IS RUNNING do a ps -a to get the pid's of each SWG executable. And take note of which ones are which.

After you cleanly exit (shutdown) the server, and ctrl+c the LoginServer, move each output-pid.profraw to a folder named for it's process.

Then, proceed to combine them into usable profiles for the compiler:

llvm-profdata merge -output=code.profdata output-*.profraw

Finally, then replace the profdata files with the updated versions, within the src/ tree.

See http://clang.llvm.org/docs/UsersManual.html#profiling-with-instrumentation for more information.

# More Information

See https://github.com/Galaxies-Reborn for more information on the Galaxies Reborn project.

Submit contributions and questions to this Galaxies-Reborn repository.

## Galaxies Reborn community

Join the [Galaxies Reborn Discord](https://discord.gg/CEwKVvKxK5) for project discussion, support, and announcements.

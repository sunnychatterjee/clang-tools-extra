=============
What is this?
=============
Clang tool that counts calls to methods in a c++ program.

================
How do I run it?
================
It's a standalone tool which can be run over a single file, or a specific subset of files, independently of the build system
Copy the exe locally. e.g. c:\tmp\call-counter.exe

1) Open a developer command prompt and setup the VC environment (something like)
   "C:\Program Files (x86)\Microsoft Visual
Studio\Preview\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
2) Set the results path:
   set CALLCOUNT_OUTPUT_PATH=c:\tmp\callcounts.csv
3) Set the analysis exclude path
   set CAExcludePath=%INCLUDE%
4) Run: call-counter.exe test.cpp -verbose --

NOTES:
- Omit -verbose if you don't want a verbose output
- Delete the output file if it exists before running - new file results are appended to the output.

If you want to flatten the result use Excel and a Pivot Table or run the following in PowerShell on your file:
$items = Import-Csv -Header @("Type","Call","Count","BranchCount") c:\tmp\callcounts.csv
$results = $items | group Type, Call | select @{n="Type";e={$_.Group[0].Type}}, @{n="Call";e={$_.Group[0].Call}}, @{n="Count";e={$_.Group | Select -ExpandProperty Count | Measure -Sum | Select -ExpandProperty Sum }}, @{n="BranchCount";e={$_.Group | Select -ExpandProperty BranchCount | Measure -Sum | Select -ExpandProperty Sum }} | ? { $_.Count -gt 1 } | Sort -Property @{e="Type";Descending=$false}, @{e="Count";Descending=$true}
$results | export-csv -NoTypeInformation "MergedMethodCalls.csv"

====================================
How do I contribute to this project?
====================================

1. Get Scoop (makes it easy to pull dependencies)
==============================================
https://scoop.sh/

Use scoop to install any of the below dependencies you don't have installed
scoop install <the thing>
- git
- cmake
- Python

2. Enlist in llvm & clang
======================
md llvm_enlist
cd llvm_enlist

git clone http://llvm.org/git/llvm.git
cd llvm\tools

git clone http://llvm.org/git/clang.git
cd clang\tools

git clone https://github.com/sunnychatterjee/clang-tools-extra extra
cd extra


3. Get ready to build
==================
cd to llvm_enlist (the root)
md build
cd build
cmake ..\llvm -G "Visual Studio 15 2017" -A x64 -Thost=x64
start  LLVM.sln


4. Editing in Visual Studio
========================
- search for the Project 'call-counter' and open 'call-counter-main.cpp'
- right-click on the project and build just that one
  (IT TAKES FOREVER, DON'T BUILD THE WHOLE SOLUTION)

5. Run the tool
============
- build output goes here 'build\Debug\bin\call-counter.cpp'
- run the tool
  call-counter.exe test_file.cpp --

- NOTE: those trailing dashes are important, they tell the check to ignore a compilation DB.


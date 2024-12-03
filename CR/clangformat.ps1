Get-ChildItem -Recurse *.h |
     foreach {
         & 'clang-format.exe' -i -style=file $_.FullName
     }

Get-ChildItem -Recurse *.hpp |
     foreach {
         & 'clang-format.exe' -i -style=file $_.FullName
     }

Get-ChildItem -Recurse *.ixx |
     foreach {
         & 'clang-format.exe' -i -style=file $_.FullName
     }

Get-ChildItem -Recurse *.c |
     foreach {
         & 'clang-format.exe' -i -style=file $_.FullName
     }

Get-ChildItem -Recurse *.cpp |
     foreach {
         & 'clang-format.exe' -i -style=file $_.FullName
     }
     
Get-ChildItem -Recurse *.cxx |
     foreach {
         & 'clang-format.exe' -i -style=file $_.FullName
     }

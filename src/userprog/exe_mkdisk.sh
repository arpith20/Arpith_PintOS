cd build
pintos-mkdisk filesys.dsk --filesys-size=2
pintos -f -q
pintos -p tests/userprog/args-none -a args-none -- -q
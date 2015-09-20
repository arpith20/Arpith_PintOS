make
cd build
if [ $? -eq 0 ];
then
	pintos -q run 'args-none A B'
fi
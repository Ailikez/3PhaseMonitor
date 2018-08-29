for f in *.html
do
	xxd -i $f | sed -e 's/unsigned char/const char/' | sed -e 's/};/,0x00};/' > ${f%.html}.h
done

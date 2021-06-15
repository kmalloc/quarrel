set -e
for i in `seq 1 1000`;do
	echo @@@@@@@@@@@@@@@@
	echo @@@@@@@@@@@@@@@@
	echo "$i-th run starting @@@@@@@@@@@@@@@@@@@@@@@@"
	./quarreltest || break
done

echo "done running, total run:$i"


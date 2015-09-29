#!/usr/local/bin/bash

#format the backup file name as the date and time
backupfile="`date +%F-%H-%M-%S`-$1"

#check for all command line arguments
if [[ $# -ne 4 ]]; then
    	echo "Illegal number of parameters"
	echo "Usage: ./backup.sh file backupdir interval-secs max-backups"
	exit 1
fi

#check if max backups is > 0
if [ $4 -lt 1 ]; then
	echo "You cannot have a less than 1 max-backups"
	exit 1
fi

#make directory
mkdir -p $2

#copy file into backup directory
cp $1 $2

#get the number of files in the backupdir
numfiles=($2/*)
numfiles=${#numfiles[@]}

#check to see if number of backup files is greater than max-backups
if [ $numfiles -gt $4 ]; then
	#more backup files than max, delete oldest files until number of files reaches max
	while [ $numfiles -gt $4 ]; do
		rm $2/`ls -tr $2 | head -n 1`
		let numfiles=numfiles-1
	done
fi

#rename backup file
mv $2/$1 $2/$backupfile

#infinite while loop that listens for changes in file
while [ 1 ]; do
	#time between each check
	sleep $3
	#check difference between file and backup file
	diff $1 $2/$backupfile > difference.txt
	if [ -s difference.txt ]; then
		#has content inside difference.txt, file has been changed
		#set tmp-message to the difference output
		cat difference.txt > tmp-message
		#make new backup file with new name
		backupfile="`date +%F-%H-%M-%S`-$1"
		cp $1 $2
		mv $2/$1 $2/$backupfile
		let numfiles=numfiles+1
		#delete content in difference.txt. Make file into an empty txt file
		> difference.txt
		#send email with file changes
		/usr/bin/mailx -s "Back up file saved" $USER < tmp-message

		#check if max-backups have been reached
		if [ $numfiles -gt $4 ]; then
			#removes the oldest file in the directory
			rm $2/`ls -tr $2 | head -n 1`
			#update numfiles after deletion  with new number of backup files
			numfiles=($2/*)
			numfiles=${#numfiles[@]}
		fi
	fi
done

Name: William Huang

Working Features:

1. Script will create new directory if backupdir does not exist yet
2. The backup file name is in the format YYYY-MM-DD-hh-mm-ss-filename
3. Script will delete the oldest backup if max-backup is reached
4. Email sends after every file change and includes the difference between files

Extras:

1. Script will check to see if all 4 command line arguments are entered
2. Script will output an error message if the max-backups is 0
3. When starting script, if there are files that exist in the backupdir already and the number of backup files that exists is already more than max-backups, then the script 
will delete n of the oldest files until the size of the directory reaches max-backups.



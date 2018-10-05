KEY=username=[yourusername],password=[yourpassword]

#shopt -s nocaseglob nocasematch

#Are block devices mounted

if [[ ! -d /mnt/block-devices ]]; then
        sudo mkdir -p /mnt/block-devices
        cd /opt/block-fuse
        ./block-fuse /dev/mapper /mnt/block-devices
else
        cd /opt/block-fuse
        ./block-fuse /dev/mapper /mnt/block-devices
fi

#Is backup directory mounted

if [[ ! -d /mnt/borgbackups ]]; then
        sudo mkdir -p /mnt/borgbackups
        sudo mount -o rw,$KEY -t cifs [your_external_storage_address] /mnt/borgbackups
else
        sudo mkdir -p /mnt/borgbackups
        sudo mount -o rw,$KEY -t cifs [your_external_storage_address] /mnt/borgbackups
fi

#Change to Directory containing Borg Repos
cd /mnt/borgbackups/$HOSTNAME

#Our Repos our in separate directories so we can make an array of repos by making an array of directories.
REPOSTORECOVER=($(find . -maxdepth 1 -type d))
unset REPOSTORECOVER[0]
# get length of an array
tLen=${#REPOSTORECOVER[@]}+1

#Using a for loop we can parse over our borg repos recovering the lastest backup
for (( i=1; i<${tLen}; i++ ));
do
  #The array we create includes the directory names and extensions, we need to remove the extension
  REPO=$(echo ${REPOSTORECOVER[$i]} | tr -d './')
  cd $REPO/
  
  #By making a list of all of the archives in the backup we are able to select the latest one
  mapfile -t repoarchives < <( sudo borg list --short /mnt/borgbackups/"$HOSTNAME"/"$REPO" )
  sudo borg extract /mnt/borgbackups/"$HOSTNAME"/"$REPO"/::"${repoarchives[-1]}"
  
  #Before we recover the extracted repo we need to make some variables, these will give us the configuration of the LV and VM that we are recovering
  LATESTLVINFO=$(ls -t *.lvdisplay | head -1)
  LATESTVMXML=$(ls -t *.xml | head -1)
  LVSIZE=$(cat "$LATESTLVINFO" | grep GiB | sed 's/[^0-9,.]*//g')

  #Here we move to where our repo has extracted
  cd /mnt/borgbackups/"$HOSTNAME"/"$REPO"/mnt/block-devices
  #Create the LV using the config extracted from the backed up lvdisplay file
  sudo lvcreate --size "$LVSIZE"G --name "$REPO"
  #We will copy the backed up block to the LV partition we have created
  sudo dd if=main--vg-"$REPO"--snap of=/dev/main-vg/"$REPO"
  #Now we can remove the extracted block
  sudo rm main--vg-"$REPO"

  #Move back to our main repo directory
  cd /mnt/borgbackups/"$HOSTNAME"/"$REPO"
  #Create the Virtual Machine using the backed up xml information
  virsh create "$LATESTVMXML"
  
  #Move back to our main working directory.
  cd /mnt/borgbackups/$HOSTNAME
done



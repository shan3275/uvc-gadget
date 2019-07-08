#!/bin/sh
if [  -f "/proc/scsi/usb-storage/0" ]; then
	echo  'u disk exist'
	mountInfo=`mount |grep usb`
	if [ "" != "$mountInfo" ];
	then
		echo 'have mounted '
		ehco 'ok'
		procID=`ps -aux |grep uvc |grep -v grep`
		if [ "" == "$procID" ];
		then
			echo '启动uvc'
			/home/pi/work/uvc-gadget/uvc >> /dev/zero &
		fi

	else
		echo 'not mounted'
		mkdir -p /mnt/usb
		mount /dev/sda1 /mnt/usb
		if [  -f "/mnt/usb/test-640x480.mp4" ]; then
			killall -9 uvc
			killall -9 ffmpeg
			sleep 2
			rm -rf /var/video/*
			cp /mnt/usb/usb/test-640x480.mp4 /var/video/
			echo 'restart uvc'
			/home/pi/work/uvc-gadget/uvc >> /dev/zero &
	fi
else
	echo 'u disk not exist'
	mountInfo=`mount |grep usb`
	if [ "" != "$mountInfo" ];
	then
		echo 'have mounted,must umount '
		umount /mnt/usb
	else
		echo 'not mounted, ok'
	fi
fi
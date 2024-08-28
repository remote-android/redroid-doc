# Deploy redroid on openKylin

```
##############################
## openKylin 2
##############################

chmod 666 /dev/{,hw,vnd}binder

### start redroid
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:11.0.0-latest

### NOTE:
### You can start only one redroid container at the same time.
### Try enable `binderfs` if want to start unlimit redroid containers.
```

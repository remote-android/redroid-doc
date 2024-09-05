# Deploy redroid on Gentoo

```
## tested on 5.18.5-gentoo-dist kernel
## binderfs etc. already enabled

## run redroid
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:12.0.0_64only-latest
```

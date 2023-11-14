# Deploy redroid on AWS EKS



**CNI**

Install EKS with Calico networking to make redroid work on EKS cluster. redroid does not work well with AWS VPC CNI

**AMI Type**

Create self-managed nodegroup to be able to use Ubunu based EKS nodes.

Use Ubuntu based EKS AMI with the following user-data section in Launch template to load require modules. Replace MYCLUSTERNAME with your cluster name

You can get the latest ubuntu ami from https://cloud-images.ubuntu.com/docs/aws/eks/ 


**User Data**
```
MIME-Version: 1.0
Content-Type: multipart/mixed; boundary="==MYBOUNDARY=="

--==MYBOUNDARY==
Content-Type: text/x-shellscript; charset="us-ascii"

#!/bin/bash
echo "Running custom user data script"
apt update
apt install -y linux-modules-extra-`uname -r`
modprobe binder_linux devices="binder,hwbinder,vndbinder"
### optional module (removed since 5.18)
modprobe ashmem_linux
/etc/eks/bootstrap.sh MYCLUSTERNAME
--==MYBOUNDARY==--
```


# Deployment

```
kubectl apply -k kubernetes/overlays/<VER>
```

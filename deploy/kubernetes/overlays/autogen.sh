#!/bin/bash

REPLICAS=1

for ver in 8.1.0 {9..13}.0.0 {12..12}.0.0-64only
do

    base_ver=`echo $ver | cut -d'.' -f 1`
    [[ $ver == *"-64only" ]] && base_ver+='-64only'

    [ -d $ver ] || mkdir $ver
    cat > $ver/kustomization.yaml <<-END
bases:
- ../../base

images:
- name: redroid/redroid
  newTag: $ver-latest

patches:
- target:
    kind: StatefulSet
  patch: |-
    - op: replace
      path: /metadata/name
      value: redroid$base_ver
    - op: replace
      path: /spec/replicas
      value: $REPLICAS
    - op: replace
      path: /spec/selector/matchLabels/app
      value: redroid$base_ver
    - op: replace
      path: /spec/template/metadata/labels/app
      value: redroid$base_ver
END

done

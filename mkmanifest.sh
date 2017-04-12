copyfile() {
    install -v -T -D -o root -g root $ROOT/fs/$1 $1
}

include() {
    echo "Including manifest $1"
    . $ROOT/manifests/$1
}

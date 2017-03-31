copyfile() {
    install -v -T -D -o root -g root ../fs/$1 $1
}

include() {
    echo "Including manifest $1"
    . ../manifests/$1
}

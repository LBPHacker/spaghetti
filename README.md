# spaghetti

A small Lua module for [The Powder Toy][1] that implements "0D megastack
synthesis", whatever that is supposed to be.

## building

```sh
meson setup -Dbuildtype=release -Dinstall_dir=/path/to/lua/module/dir build
cd build
ninja
ninja install # needs write privileges to install_dir
```

## usage

KS adder demo in [here][2].
```sh
./examples/ks.lua
```

[1]: https://github.com/The-Powder-Toy/The-Powder-Toy
    "The Powder Toy on GitHub"
[2]: examples/ks.lua
	"KS adder demo"

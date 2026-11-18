Run the widget tests with:

```
make -C tests/hexpat -j8 check
```

To also test evaluation, relative and explicit includes, nested fields, pointers,
bitfields, large integers, reloading, and error handling against r2hexpat:

```
HEXPAT_PLUGIN=/absolute/path/to/hexpat.r2.js make -C tests/hexpat -j8 check
```

The tests use an isolated radare2 core and temporary settings and pattern files.

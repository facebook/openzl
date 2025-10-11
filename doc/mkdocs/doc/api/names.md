All OpenZL graph components (nodes & graphs) have names. Each name is unique
and uniquely identifies a particular graph component. There are three ways to
give a graph component a name:

1. Give it an explicit name. Explicit names begin with `'!'`. The graph
   component will be named with the provided name (minus the `'!'`). OpenZL
   will not allow other components to be created in the same compressor with the
   same explicit name. This means that it can be looked up by that name.
2. Give it a base name. Base names don't begin with `'!'`. Multiple graph
   components may be created with the same base name. OpenZL will append a
   suffix (of the form `"#n"`, where `n` is an unsigned integer) to the base
   name to produce a unique name for each instance. The name that the base-named
   graph component receives will be different than the base name, and is not
   stable between `ZL_Compressor`s, which means that you can't ever look up a
   component by just the base name, nor can you use the name that it's given in
   one `ZL_Compressor` to look up or identify the same component in a different
   `ZL_Compressor`.
3. Let OpenZL pick a name. Some entry points for creating new graph components
   let you decline to provide a name. In the case that you don't provide one,
   OpenZL will assign a (possibly not very meaningful) unique name. Other entry
   points don't let you provide a name at all, and instead implicitly name the
   new object based on the component they enclose / were based on / modify.
   Whether that component had an explicit or base name, OpenZL will name the
   new component by appending or updating the suffix to create a new unique
   name. These assigned names are similarly not stable across `ZL_Compressor`
   instances.

## Rules

- Explicit names begin with the `'!'` character, and base names do not. The
  `'!'` character may not appear elsewhere in the name.
- Names must not contain the `#` character. This character is used for
  generating unique names from base names.
- User-defined explicit names must not begin with `!zl.`. This is the prefix for
  all explicit names defined within OpenZL. However, user-defined base names
  may begin with `zl.`. This can happen, for example, when cloning a standard
  node or graph without providing a new name.

## Standard Names

All public Standard nodes and graphs have explicit names. Given their enum name,
the name is `!zl.${name}`. For example `!zl.field_lz`.

## Looking up Nodes and Graphs by name

When looking up graph components, you use the unique name. This is equivalent to
the explicit name minus the leading `'!'`, or the base name with the appended
unique suffix (if you know it). For example, you could look up `zl.field_lz` or
`my_node#2`.

::: ZL_Compressor_getNode

::: ZL_Compressor_getGraph

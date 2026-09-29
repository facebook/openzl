# Definitions

## Code

The OpenZL codebase, as deployed in production. E.g. "Nimble is currently using v1.X.X of the **code**".

## Library Version

The current version of the code.

## Component

An OpenZL node or graph. It must store its **minimum library version**, which is defined below.

## Config

A serialized compressor along with application specific metadata. E.g. "Scribe is using this **config** to compress its data". The **minimum library version** of a config is the maximum of its components' **minimum library versions**.

## Backward Compatibility

**Configs/components** are **backward compatible** if new **configs/components** can be understood by old **code**.

## Forward Compatibility

**Configs/components** are **forward compatible** if old **configs/components** can be understood by new **code**.

## Minimum Library Version

The **minimum library version** for an OpenZL **component** is the oldest version of the **code** that can understand **configs** containing the **component** produced by the current **code**.

# OpenZL Library Versions

OpenZL is designed to work reliably in production systems where both new binaries and new **configs** are continually deployed while older binaries remain in service.

To support this, compressors are serializable. This decouples training from deployment: a trained compressor is serialized, transported as a config, and deserialized at the destination. Because old configs may be used by new binaries (and new configs by old binaries), the serialized compressor must maintain both **forward** and **backward compatibility**.

Each **component** in OpenZL records its own **minimum library version**. With a few policies in OpenZL and following a few strategies in production, these version markers help ensure that production deployments are safe and reliable.

## Forward Compatibility

**Components** MUST maintain full **forward compatibility**. This means for all configs that contain that component either:

- Old configs can still be understood by new code
- There are no existing **configs** that require **forward compatible** behavior.

OpenZL standard components will make a best effort to remain understandable by new code.

## Backward Compatibility and Minimum Library Version

**Components** MUST maintain full **backward compatibility** down to their **minimum library version**. To ensure this, a backward-incompatible change MUST bump the **minimum library version** in the current **code** to the current library version. This explicitly marks older code as incompatible with configs containing that component.

### Backward Compatibility - Handling Incompatible Configs

A newly deployed config might be incompatible with older code still running in production. This SHOULD be addressed by one or more of the following strategies:

- **Targeted rollout of configs**: Only rolling the new config to new libraries
- **Deploying a compatible config**: Replace the new config with an older config that is compatible with the existing code.
- **Updating binaries:** Ensure old **code** that does not meet **minimum library version requirement** of deployed configs is no longer present in the production environment

Using known working compressors as fallbacks is the recommended solution. Updating all instances of old code can sometimes be challenging.

## Updating Component Minimum Library Version

It may be necessary to update existing components with changed performance characteristics while it is undesirable to accept the maintenance burden of retaining multiple versions of that component. Refer to the **Component Version Update Policy** for more details. In such cases, the minimum library version of the component should be updated to the current library version when making the change.

When a component's minimum library version is updated, a newer config may show unexpected performance on an older library version and vice versa. Refer to the backward compatibility section on recommended strategies to handle the newer config. On cases where a newer library receives a config older than the minimum library version of its components, compression performance is no longer guaranteed but the compressor will be fully functional. Such cases SHOULD be flagged for the config to be retrained with a newer library.

## Component Version Update Policy

The library version MUST be updated on the addition of any new standard component, and the minimum library version of that component MUST be set to the new library version.

Certain changes to compression SHOULD update the minimum library version for the component. Generally changes of this nature are:

- Changing the overall behavior of the algorithm. It is subjective whether or not a change requires a version update in this case.
- Significant performance changes on a standard component. Performance refers to compression and decompression speed as well as compression ratio.

A component may instead choose to support multiple versions. This should be done by creating a new component with a different name.

If it is unclear according to the policies whether the library version should be bumped, the correct course of action is left up for review.

### Local Parameters
- New local parameters that represent new features in a standard graph do not require a library version update.
- All standard components MUST make all new local parameters have a reasonable default.
- **Removing a local parameter** requires a library version update.
- **Changing the structure of a local parameter** with the same id **requires a library version update**.
- Such an update MUST be done by supporting both structures using different parameter ids.

### Illegal Changes

- A graph MUST NOT be updated in a way that causes valid inputs of older versions of that graph to fail compression
- A name of an existing OpenZL component MUST NOT be changed

## Examples

**Examples of library version update requiring changes:**

- Updating the default compression level used for a component
- Expanding the format the graph parses. For example csv parsing with required spaces to generalized csv parsing
- Switching the numeric selector backing technology resulting in compression ratio improvements in some data sets but regression in other data sets.

**Examples of changes that do not require version bump:**

- A transitive version change (this is tracked by the updated codec/graph itself)
- A minor bugfix
- Small performance optimizations

---

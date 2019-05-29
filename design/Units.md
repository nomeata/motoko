## Motivation

* Provide a coherent model for what a source file is
* Support actors, libraries, and programs
* Define the semantics of imports
* Allow separate compilation and (potentially) dynamic linking
* Resolve [issue #400](https://github.com/dfinity-lab/actorscript/issues/400)


## Overview

A single AS file is called a *unit*. There are three use cases of a unit:

1. *Programs*: stand-alone AS scripts that are meant to be run, e.g., for tests; also, the input to the REPL.

2. *Libraries*: components that are meant to be imported into other units.

3. *Actors*: stand-alone actors that are meant to be deployed to the platform as part of a canister.

As an additional requirement, we want to be able to dual-use an actor source file as a library, e.g., to write unit tests.

The aim of this proposal is to make each of these use cases possible and convenient while unifying them as much as possible.


### Context

This proposal is essentially a mixture of A and B as suggested [here](https://github.com/dfinity-lab/actorscript/issues/400#issuecomment-492195603). It is mostly like B, except that libraries are as in A.

This has two advantages over both of these:

* It avoids the need to wrap every library module file into a tedious `module` expression (which in principle also implies that its entire content would have to be indented).

* It allows actor unit source files to play the dual role as a library module for import into a test program.

The proposal also has some flavour of C, in that imports now have a special status and can prefix the definition of the actor in an actor unit. (Like with B vs C, we could also extend this proposal further to allow static declarations in an actor unit. But for now I suggest to remain conservative and forbid that.)


## Syntax and Semantics

The syntax of a unit -- and therefore an AS source file -- is a sequence of *imports* followed by a sequence of *field definitions* (as in a module or object body).

```
<unit>   ::= <imp>;* <field>;*

<imp>    ::= import <imppat> =? <text>
<imppat> ::= <id> | { <id>;* }

<field>  ::= public? <dec>
```

Each import binds the identifiers in its *import pattern*. If the pattern is a plain `<id>` then the contents of the imported unit is reified as a module object bound to that id. If the pattern is a *destructuring* import pattern then the respective public fields of the unit are bound to the respective label identifiers.

As a crucial restriction, a unit that has at least one public field must be *static*, see below.

Notes:

* This inverts the current public/private default, as it should.
* The optional `=` in an import may be removed as a more general syntax cleanup.
* There are various ways in which we might extend the syntax of import patterns, e.g., allowing type annotations or allowing field patterns of the form `<id> = <id>` to support renaming. This is just the most basic form.


### Programs

Programs are expressed as units that have no public fields.

A program executes by evaluating its field declarations in sequence, triggering respective side effects.


### Libraries

A library is a unit that has at least one public field.

That implies the additional requirement that all its declarations must be *static*. This is a syntactic approximation guaranteeing the freedom from state and side effects other than non-termination or uncatchable traps (similar to the value restriction in ML).

With this restriction, it becomes unobservable whether a library module is instantiated and linked multiple times. That is relevant for cases such as compiling local actors, which can close over imports by simply relinking them. For this purpose, the prelude can be treated as a library module that is implicitly imported.

Other than this restriction, there is no difference between programs and libraries.


### Actors

An actor unit must contain exactly one field declaration, which is either a manifest actor class or (as a short hand) an actor. It can also have imports.

The actor or actor class may be named or given as an anonymous expression. It may also be either public or private. Writing it as a public named declaration allows the same unit source file to be viewed and compiled as a library module, e.g., for the purpose of importing it into a unit test.

Even if named, an actor class defining an actor unit is not allowed to refer to itself recursively. (If necessary, this restriction may be lifted later.)


## Compilation

All units are compiled to Wasm modules.
Their public fields become Wasm exports.
These are either Wasm functions, for public fields of function type,
or Wasm globals, for all others.
Compiling arbitrary closures into exported Wasm functions may require eta-expanding the closure and storing its environment into an internal global.

Imports expect the import URL to resolve to a Wasm module and link its exports accordingly.
An import that is not destructured via a module pattern is reified into a module object at the import site.

Programs and libraries are compiled exactly the same.
The above scheme is all that is needed.

Actors are different.
Their exported functions are wrapped into methods de/serialising their arguments and results according to the IDL epcification.
Furthermore, they are complemented with system exports for initialising the actor (given the actor class'es arguments) and for in/externalising the actor's state for upgrades (details TBD).

We may want to mark the difference between libraries and actors in a custom section.


### Compiler

We need two compilation modes, one for programs/libraries, the other for actors. We could differentiate based on file extensions, but that would get in the way of dual-using an actor source file as a library.

Hence we will need different compiler mode flags, e.g.:

* `-c`: compile as program or library
* `-a`: compile as actor
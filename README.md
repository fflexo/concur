This is a simple utility to limit the number of concurent instances of a process.

The idea being that when you run commands like:

    find /path/to/big/directory -execdir slow_task {} \;

it would be neat to just add a -j option when the execution is being used for batch processing instead of as a predicate.

Yes you can achieve that already with makefiles or xargs, e.g.

    find /path/to/big/directory -print0 | xargs -0 -n 1 -P 4 slow_task

but that's an awful lot of extra typing isn't it?

With concur we can now write something as short and simple as this:

    find /path/to/big/directory -execdir concur slow_task {} \;

and it'll automatically run one task per CPU.

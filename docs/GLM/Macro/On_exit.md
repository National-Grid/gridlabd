[[/GLM/Macro/On_exit]] -- Macro to handle exit conditions

# Synopsis

~~~
#on_exit exitcode command
~~~

# Description

When this macro is encountered, an exit handler is added when the simulation exits with `exitcode`.  Exit handlers are run only when the simulation has completed all post simulation processing and is about to exit.  The exit process is suspended until the result of the exit handler(s) are obtained. The exit code of the exit handler is then used as the exit code of the simulation.  

If multiple exit handlers are specified, they will be called in the order in which they are listed. If an exit handler returns a non-zero exit code, the exit handling sequence is abandoned and the exit code of the simulation is the exit code of the failed handler.  If no errors are encountered, the exit code 0 is used. 

When the value `-1` is used for the `exitcode`, any non-zero exit condition will trigger the exit handler.

## Example

The following example run a series of models with VARIABLE incremented for each run

~~~
// set the initial value to 0
#ifndef VARIABLE
#define VARIABLE=0
#endif
// print the value
#system echo ${VARIABLE}
// run the next value, stopping at 9
#if VARIABLE < 9
#on_exit 0 ${exename} -D VARIABLE=$((${VARIABLE}+1)) ${modelname} &
#endif
~~~

which produces the output

~~~
0
1
2
3
4
5
6
7
8
9
~~~

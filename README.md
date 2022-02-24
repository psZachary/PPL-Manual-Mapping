# PPL-Manual-Mapping
(C++ 17) Pre Process Load Manual Mapping, starts the process supended, maps the DLL, and resumes the process. Useful for loading the DLL before the process starts. Setting a sleep timer on the DLLMain function may solve crashing issues if they exist. Proven to bypass some game anti cheats that check for newly allocated memory <b>AFTER</b> the application has started.  
<br>
Forked from: https://github.com/TheCruZ/Simple-Manual-Map-Injector
<br>
![Showcase](https://github.com/Fozzila/PPL-Manual-Mapping/blob/master/ss.png)


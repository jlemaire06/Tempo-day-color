# Tempo-day-color
The Tempo option proposed by EDF depends on the Tempo day color, fixed each day by RTE in France.
In a photovoltaic system, it may be usefull to know the Tempo Day J and J+1 color, in view to optimize routing for instance.
RTE exposes a public API, Tempo Like Supply Contract, enabling an access to the Tempo days colors, which dates are between a
StartDate and an EndDate, and the data are returned in a JSON format.
This mecanism is explained in details and the access to the API is implemented in C++ (program for PlatformIO in VSCode), using an ESP32.


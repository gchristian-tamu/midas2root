# midas2root
Unpacker to create simple ROOT trees from UK-MIDAS data files.

# Usage
Start a ROOT session and issue the following commands
```
.L midas2root.cxx+
midas2root(<midas file>, <root file>, <channel map file>, base, offset);
```
The inputs are:
   `<midas file>` Path to input MIDAS file (needs to already be unzipped).
   `<root file>` Path to output ROOT file (your choice of filename). Silently over-writes an existing file with the same path.
   `<channel map file>` Path to CSV file containing channel mapping info (see below for more).
   `base` Integer specifying the "ADC Base" for this run (should be set as part of the DAQ setup).
   `offset` Integer specifying the "ADC Offset for this run.
   
# Map File
The mapping of ADC ID+Channel into ROOT branches is accomplished with a CSV file. The file should be three columns, and the first row (a header) is ignored. The column format is:
`BRANCH NAME,ADC ID,CHANNEL NUMBER`

The ADC ID and Channel number correspond to parameters set in the DAQ setup. The branch name specifioes the branch in the ROOT tree into which data with the corresponding ID and channel number are stored.

**Warning** - the map file contains no way to comment. The only thing ignored is the first line of the file.

# ROOT Tree Structure
Each branch in the tree is a `vector<unsigned int>`. Successive entries in each vector correspond to successive "hits" in a given ADC+Channel. For a given event, vectors will be empty if there were no hits in that channel.

# Configuration files with allowed options list

Currently `tpcGUI` takes [JSON](https://en.wikipedia.org/wiki/JSON#Syntax) formatted configuration file as input.

## Formatting requirements

The first(main) [JSON](https://en.wikipedia.org/wiki/JSON#Syntax) object in a config file has to be called "Options", a list of allowed options and command line arguments is a list of JSON objects within "Options" object. Every option has to have four values specified: type (currently known: int, unsigned int, float, double, bool, std::string, std::vector<std::string>), defaultValue, description (string) and isRequired(bool).



## Example

```json
{   "Options":
    {
        "pressure": {
            "type": "float",
            "defaultValue": 60,
            "description": "float - CO2 pressure [mbar]",
            "isRequired": true
        },
        "no-type": {
            "type": "bool",
            "defaultValue": false,
            "description": "Skip comparing event type",
            "isRequired": false
        },
        "files": {
            "type": "std::vector<std::string>",
            "defaultValue": "PLACEHOLDER_FOR_VALUE",
            "description": "strings - list of files to browse. Mutually exclusive with 'directory'",
            "isRequired": false
        }
    }
}
```
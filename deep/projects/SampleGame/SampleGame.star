{
  "schemaVersion": 3,
  "name": "SampleGame",
  "module": "SampleGame.dll",
  "arguments": [
    "--models",
    "D:\\projects\\SampleGame\\starlight-realms\\build\\models",
    "--textures",
    "D:\\projects\\SampleGame\\starlight-realms\\build\\textures\\bakes-v1"
  ],
  "defaultScene": "club",
  "scenes": {
    "crowd": {
      "arguments": [
        "--stress",
        "100000",
        "--camdist",
        "800",
        "--camhigh",
        "650"
      ],
      "checks": {
        "equals": {
          "culled": 0
        },
        "minimum": {
          "skinned": 100000
        }
      }
    },
    "club": {
      "arguments": [
        "--stress",
        "0",
        "--camdist",
        "9",
        "--camhigh",
        "5"
      ],
      "checks": {
        "minimum": {
          "skinned": 8
        }
      }
    },
    "live-bar": {
      "arguments": [
        "--stress",
        "0",
        "--camdist",
        "9",
        "--camhigh",
        "5",
        "--auto"
      ],
      "requires": "Live server connection; forwarding is supplied separately.",
      "checks": {
        "equals": {
          "server": true
        },
        "minimum": {
          "crowdParsed": 40
        }
      }
    }
  },
  "testCapabilities": [
    "resolution",
    "displayMode",
    "frameLimit",
    "durationMs",
    "upscaler",
    "quality",
    "frameGeneration",
    "vsync",
    "culling",
    "foreground"
  ]
}

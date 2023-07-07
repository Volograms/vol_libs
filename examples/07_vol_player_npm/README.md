# examples/07_vol_player_npm

This is an example project to demonstrate how to import and use the vologram player, as well as the various extensions, through the npm package.

As such, npm is a requirement. If you do not have it installed but still wish to see an example of the volograms player working, consider looking at [example 05_vol_player_wasm](../05_vol_player_wasm/).

To run this example, first run an npm install:

`npm i`

Then you have a choice of examples to launch:

- `npm run gl`: vologram player with WebGL extension, using the default values
- `npm run gl-scene`: vologram player with WebGL extension, but using your own scene
- `npm run three`: vologram player with Three.js extension, using the default values
- `npm run three-scene`: vologram player with Three.js extension, but using your own scene

Running one of these commands with launch the project to your localhost, open the browser of your choice and open the address npm tells you, and there you have it.

Some controls:

- the `single file` checkbox at the top allows to choose from 2 different example volograms
  - if deactivated, a vologram with a header, sequence and texture file will be opened (you'll be able to see the video texture player in the video element)
  - if activated, a vologram with a single file will be opened (you'll be able to see the audio of the vologram playing in the audio element)
- the `load vologram` button opens the vologram
- the `play vologram` button plays the vologram if paused
- the `pause vologram` button pauses the vologram playback
- the `stop vologram` button stops and clears the vologram so you can open the other one
- the `restart vologram` button plays the vologram from the start
- the `toggle loop` button enables and disables playback looping

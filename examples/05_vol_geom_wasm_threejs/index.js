import VologramPlayer from "../../wasm/players/VologramPlayer.mjs";
import VologramThreeJsExtension from "../../wasm/players/VologramThreeJsExtension.mjs";
import * as THREE from "three"; // Initialise canvas for web drawing

const canvas = document.getElementById("canvas");
canvas.width = window.innerWidth - 50;
canvas.height = window.innerHeight - 50;

// Define vologram files paths
let vologramPlayer = new VologramPlayer();
let header_url = "../example_vologram/calif/header.vols";
let sequence_url = "../example_vologram/calif/sequence_0.vols";
let video_url = "../example_vologram/calif/output_720.mp4";
let single_file_url = "../../samples/combined.vols";

// Initialise three js rendering objects
const scene = new THREE.Scene();
const renderer = new THREE.WebGLRenderer({ canvas: canvas });

renderer.setSize(canvas.clientWidth, canvas.clientHeight);
renderer.setClearColor(new THREE.Color(147.0 / 255.0, 149.0 / 255.0, 237.0 / 255.0), 1.0);
const camera = new THREE.PerspectiveCamera(50, canvas.clientWidth / canvas.clientHeight, 0.1, 100);
camera.position.set(0.0, 1.2, -5.0);
camera.lookAt(0.0, 1.2, 0.0);
scene.add(camera);

// Create video element that will serve has the texture for the vologram
var video_element = document.getElementById("texture_video_el");

// three js render function

function render() {
	renderer.render(scene, camera);
}

var load_button = document.getElementById("load_vols_files_button");
var stop_button = document.getElementById("stop_vols_files_button");

//vologramPlayer.open(header_url, sequence_url, video_url, console.log).then(() => {
vologramPlayer.openSingleFile(single_file_url, console.log).then(() => {
	console.log("READY TO GO");
	if (!vologramPlayer.vologram.header.singleFile) vologramPlayer.attachVideo(video_element);
	load_button.onclick = vologramPlayer.play;
	stop_button.onclick = vologramPlayer.close;
	vologramPlayer.addEventListener("onloop", () => {
		console.log("loop");
	});
	let threeJsPlayer = new VologramThreeJsExtension(renderer, vologramPlayer);
	scene.add(vologramPlayer.vologram.three.mesh);
	renderer.setAnimationLoop(render);
});

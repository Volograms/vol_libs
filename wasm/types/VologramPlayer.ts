type Vologram = import("./Vologram").Vologram;

export type ProgressCallback = (progress: number) => void;
export type VologramEvent = "onframeready" | "onclose" | "onloop";
export type EditCallbackFunction = (event: VologramEvent, callback: (vologram: Vologram | null) => void) => void;

export type VologramPlayerExtension = {
	get name(): string;
	get init(): (vologram: Vologram) => void;
	get close(): () => void;
	exports: any;
};

export type VologramPlayerExtensionConstructor = (options: any) => VologramPlayerExtension;

export type VologramPlayer = {
	get vologram(): Vologram;
	play: () => void;
	pause: () => void;
	resume: () => void;
	isPlaying: () => boolean;
	mute: (newVal: boolean) => boolean;
	open: (
		parameters: {
			headerUrl: string | null;
			sequenceUrl: string;
			textureUrl: string | null;
			videoElement: HTMLVideoElement | null;
			audioElement: HTMLAudioElement | null;
		},
		onProgress?: ProgressCallback
	) => PromiseLike<boolean>;
	close: () => void;
	registerCallback: EditCallbackFunction;
	unregisterCallback: EditCallbackFunction;
	get extensions(): any;
};

export type VologramPlayerConstructor = (extensions: Array<VologramPlayerExtension>) => VologramPlayer;

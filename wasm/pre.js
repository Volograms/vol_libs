// This is an example of pre-js
Module.initVologramFunctions = () => {
    Module['has_normals'] = Module.cwrap('has_normals', 'boolean');
    Module['has_texture'] = Module.cwrap('has_texture', 'boolean');
    Module['texture_width'] = Module.cwrap('texture_width', 'number');
    Module['texture_height'] = Module.cwrap('texture_height', 'number');
    Module['create_file_info'] = Module.cwrap(
        'create_file_info', 'boolean',
        ['string', 'string']
    );
    Module['free_file_info'] = Module.cwrap(
        'free_file_info', 'boolean',
    );
    Module['frame_count'] = Module.cwrap(
        'frame_count', 'number'
    );
    Module['loaded_frame_number'] = Module.cwrap(
        'loaded_frame_number', 'number'
    );
    Module['read_frame'] = Module.cwrap(
        'read_frame', 'boolean',
        ['number']
    );
    Module['max_blob_sz'] = Module.cwrap(
        'max_blob_sz', 'number'
    );
    Module['is_keyframe'] = Module.cwrap(
        'is_keyframe', 'boolean', 
        ['number']
    );
    Module['find_previous_keyframe'] = Module.cwrap(
        'find_previous_keyframe', 'number',
        ['number']
    );
    Module['frame_vertices'] = Module.cwrap(
        'frame_vertices', 'array'
    );
    Module['frame_vertices_sz'] = Module.cwrap(
        'frame_vertices_sz', 'number'
    );
    Module['frame_uvs_sz'] = Module.cwrap(
        'frame_uvs_sz', 'number'
    );
    Module['frame_normals_sz'] = Module.cwrap(
        'frame_normals_sz', 'number'
    );
    Module['frame_texture_data_ptr'] = Module.cwrap(
        'frame_texture_data_ptr', 'number'
    );
    Module['frame_texture_sz'] = Module.cwrap(
        'frame_texture_sz', 'number'
    );
    Module['frame_indices'] = Module.cwrap(
        'frame_i', 'array'
    );
    Module['frame_i_sz'] = Module.cwrap(
        'frame_i_sz', 'number'
    );
    Module['frame_data_ptr'] = Module.cwrap(
        'frame_data_ptr', 'array'
    );
    Module['frame_vp_offset'] = Module.cwrap(
        'frame_vp_offset', 'number'
    );
    Module['frame_vp_copied'] = Module.cwrap(
        'frame_vp_copied', 'array'
    );
    Module['frame_uvs_copied'] = Module.cwrap(
        'frame_uvs_copied', 'array'
    );
    Module['frame_normals_copied'] = Module.cwrap(
        'frame_normals_copied', 'array'
    );
    Module['frame_indices_copied'] = Module.cwrap(
        'frame_indices_copied', 'array'
    );
}

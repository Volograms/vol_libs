// This is an example of pre-js
Module.initVologramFunctions = (containerObject) => {
    let container = containerObject ? containerObject : Module;
    container['has_normals'] = Module.cwrap('has_normals', 'boolean');
    container['has_texture'] = Module.cwrap('has_texture', 'boolean');
    container['texture_width'] = Module.cwrap('texture_width', 'number');
    container['texture_height'] = Module.cwrap('texture_height', 'number');
    container['create_file_info'] = Module.cwrap(
        'create_file_info', 'boolean',
        ['string', 'string']
    );
    container['free_file_info'] = Module.cwrap(
        'free_file_info', 'boolean',
    );
    container['frame_count'] = Module.cwrap(
        'frame_count', 'number'
    );
    container['loaded_frame_number'] = Module.cwrap(
        'loaded_frame_number', 'number'
    );
    container['read_frame'] = Module.cwrap(
        'read_frame', 'boolean',
        ['number']
    );
    container['max_blob_sz'] = Module.cwrap(
        'max_blob_sz', 'number'
    );
    container['is_keyframe'] = Module.cwrap(
        'is_keyframe', 'boolean', 
        ['number']
    );
    container['find_previous_keyframe'] = Module.cwrap(
        'find_previous_keyframe', 'number',
        ['number']
    );
    container['frame_vertices'] = Module.cwrap(
        'frame_vertices', 'array'
    );
    container['frame_vertices_sz'] = Module.cwrap(
        'frame_vertices_sz', 'number'
    );
    container['frame_uvs_sz'] = Module.cwrap(
        'frame_uvs_sz', 'number'
    );
    container['frame_normals_sz'] = Module.cwrap(
        'frame_normals_sz', 'number'
    );
    container['frame_texture_data_ptr'] = Module.cwrap(
        'frame_texture_data_ptr', 'number'
    );
    container['frame_texture_sz'] = Module.cwrap(
        'frame_texture_sz', 'number'
    );
    container['frame_indices'] = Module.cwrap(
        'frame_i', 'array'
    );
    container['frame_i_sz'] = Module.cwrap(
        'frame_i_sz', 'number'
    );
    container['frame_data_ptr'] = Module.cwrap(
        'frame_data_ptr', 'array'
    );
    container['frame_vp_offset'] = Module.cwrap(
        'frame_vp_offset', 'number'
    );
    container['frame_vp_copied'] = Module.cwrap(
        'frame_vp_copied', 'array'
    );
    container['frame_uvs_copied'] = Module.cwrap(
        'frame_uvs_copied', 'array'
    );
    container['frame_normals_copied'] = Module.cwrap(
        'frame_normals_copied', 'array'
    );
    container['frame_indices_copied'] = Module.cwrap(
        'frame_indices_copied', 'array'
    );
}

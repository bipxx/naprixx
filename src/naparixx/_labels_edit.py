"""Declaration and inert compiler metadata for handwritten Labels edit policy."""

from naparixx import _native


def _edit_0(self, slice_key, mask, new_label, dims_to_paint, refresh=True, region_data=None):
    _source_locals = (...).__call__(0, self, slice_key, mask, new_label, dims_to_paint, refresh, region_data); del self, slice_key, mask, new_label, dims_to_paint, refresh, region_data; return (...).__call__(_source_locals)  # noqa: E501, E702


def _edit_1(self, data, mask, new_label, volume_slice):
    _source_locals = (...).__call__(1, self, data, mask, new_label, volume_slice); del self, data, mask, new_label, volume_slice; return (...).__call__(_source_locals)  # noqa: E501, E702


def _edit_2(self, new_label):
    _source_locals = (...).__call__(2, self, new_label); del self, new_label; return (...).__call__(_source_locals)  # noqa: E501, E702


def _edit_3(self, region_data, slice_key, mask, new_label):
    _source_locals = (...).__call__(3, self, region_data, slice_key, mask, new_label); del self, region_data, slice_key, mask, new_label; return (...).__call__(_source_locals)  # noqa: E501, E702


def _edit_4(self, slice_key):
    _source_locals = (...).__call__(4, self, slice_key); del self, slice_key; return (...).__call__(_source_locals)  # noqa: E501, E702


def _edit_5(self, visible_data, visible_mask):
    _source_locals = (...).__call__(5, self, visible_data, visible_mask); del self, visible_data, visible_mask; return (...).__call__(_source_locals)  # noqa: E501, E702


def _edit_6(self):
    _source_locals = (...).__call__(6, self); del self; return (...).__call__(_source_locals)  # noqa: E501, E702


def _edit_7(self, updated_slice):
    _source_locals = (...).__call__(7, self, updated_slice); del self, updated_slice; return (...).__call__(_source_locals)  # noqa: E501, E702


def _edit_8(self, updated_slice):
    _source_locals = (...).__call__(8, self, updated_slice); del self, updated_slice; return (...).__call__(_source_locals)  # noqa: E501, E702


def _edit_9(self):
    _source_locals = (...).__call__(9, self); del self; return (...).__call__(_source_locals)  # noqa: E501, E702


def _edit_10(self, indices, value, refresh=True):
    _source_locals = (...).__call__(10, self, indices, value, refresh); del self, indices, value, refresh; return (...).__call__(_source_locals)  # noqa: E501, E702


def _edit_11(index, position_in_axes):
    _source_locals = (...).__call__(11, index, position_in_axes); del index, position_in_axes; return (...).__call__(_source_locals)  # noqa: E501, E702


def _edit_12(index, position_in_axes, indices_order):
    _source_locals = (...).__call__(12, index, position_in_axes, indices_order); del index, position_in_axes, indices_order; return (...).__call__(_source_locals)  # noqa: E501, E702


_SHELLS = (_edit_0, _edit_1, _edit_2, _edit_3, _edit_4, _edit_5, _edit_6, _edit_7, _edit_8, _edit_9, _edit_10, _edit_11, _edit_12)

# Whole original bodies below are non-executable compiler controls.
_PROFILES = ((11,
  ((('INT', 7),
    ('INT', 0),
    ('INT', 0),
    ('INT', 9),
    ('INT', 6),
    ('INT', 16777219),
    ('BYTES',
     '8704970074010000000000000000000088046601640184087403000000000000000000007c006a020000000000000000a6010000ab0100000000000000004400a6000000ab000000000000000000a6010000ab0100000000000000007d077c07722c7407000000000000000000006a0400000000000000007c027c07a6020000ab0200000000000000007d027c0681157407000000000000000000006a0400000000000000007c067c07a6020000ab0200000000000000007d067c06801f7407000000000000000000006a0500000000000000007c006a0600000000000000007c0119000000000000000000a6010000ab0100000000000000007d067c00a00700000000000000000000000000000000000000007c067c027c037c01a6040000ab0400000000000000007d087c088002640253007c067c006a0600000000000000007c013c0000007c00a00800000000000000000000000000000000000000007c067c017c087c03a6040000ab04000000000000000001007c00a00900000000000000000000000000000000000000007c01a6010000ab01000000000000000001007c0572167c00a00a0000000000000000000000000000000000000000a6000000ab00000000000000000001006402530064025300'),
    ('TUPLE',
     (('STR',
       'Universal painting method using a boolean mask within a bounding box.\n'
       '\n'
       '        This method extracts the common pattern shared by brush painting, fill\n'
       '        and polygon painting: extract a region, apply a mask, write back, update\n'
       '        caches. It manages cache updates for non-shared memory backends, respects\n'
       '        the preserve_labels setting (handled by _apply_mask_to_data), and updates\n'
       '        undo/redo history.\n'
       '\n'
       '        Parameters\n'
       '        ----------\n'
       '        slice_key : tuple of slice\n'
       '            N-dimensional slice key in volume space specifying the bounding box\n'
       '            region; length-1 slices for non-painted dims. Example:\n'
       '            (slice(5, 6), slice(100, 200), slice(150, 250)) for a 3D volume\n'
       '            where dims_to_paint=[1, 2].\n'
       '        mask : ndarray\n'
       '            Boolean mask indicating which pixels to paint within the bounding\n'
       '            box, with one axis per painted dimension.\n'
       '        new_label : int\n'
       '            Label value to paint.\n'
       '        dims_to_paint : list of int\n'
       '            Indices of dimensions being painted (e.g., [1, 2] for YX in a ZYX volume).\n'
       '        refresh : bool, optional\n'
       '            Whether to refresh the display after painting. Default is True.\n'
       '        region_data : np.ndarray | None, optional\n'
       '            Pre-loaded data for the region defined by slice_key, with one axis\n'
       '            per painted dimension. If None (default), data will be read from\n'
       '            self.data[slice_key]. Providing this avoids redundant reads when\n'
       '            data is already available (e.g. in fill).\n'
       '        '),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 3),
        ('INT', 16777267),
        ('BYTES', '95014b00010097007c005d0a7d017c0189027601af067c015600970101008c0b64005300'),
        ('TUPLE', (('NONE',),)),
        ('TUPLE', ()),
        ('TUPLE', (('STR', '.0'), ('STR', 'dim'))),
        ('TUPLE', (('STR', 'dims_to_paint'),)),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'Labels._paint_region_with_mask.<locals>.<genexpr>'),
        ('INT', 1809),
        ('BYTES', ''),
        ('BYTES',
         'f8e800e8008000f000021b0af000021b0ad81417a873b82dd02f47d02f478843d02f47d02f47d02f47d02f47f003021b0af000021b0a'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))),
      ('NONE',))),
    ('TUPLE',
     (('STR', 'tuple'),
      ('STR', 'range'),
      ('STR', 'ndim'),
      ('STR', 'np'),
      ('STR', 'expand_dims'),
      ('STR', 'asarray'),
      ('STR', 'data'),
      ('STR', '_apply_mask_to_data'),
      ('STR', '_refresh_caches_from_region'),
      ('STR', '_accumulate_updated_slice'),
      ('STR', '_partial_labels_refresh'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'slice_key'),
      ('STR', 'mask'),
      ('STR', 'new_label'),
      ('STR', 'dims_to_paint'),
      ('STR', 'refresh'),
      ('STR', 'region_data'),
      ('STR', 'extra_axes'),
      ('STR', 'effective_mask'))),
    ('TUPLE', ()),
    ('TUPLE', (('STR', 'dims_to_paint'),)),
    ('STR', '_paint_region_with_mask'),
    ('STR', 'Labels._paint_region_with_mask'),
    ('INT', 1767),
    ('BYTES', ''),
    ('BYTES',
     'f88000f5540100161bf000021b0af000021b0af000021b0af000021b0add1b20a014a419d11b2bd41b2bf003021b0af100021b0af400021b0af10002160af40002160a880af006000c16f00003094601dd1315943ea024a80ad11333d413338844d80f1ad00f26dd1e209c6ea85bb82ad11e45d41e45900be00b16d00b1edd1a1c9c2aa054a459a879d42539d11a3ad41a3a884be0191dd71931d21931d80c1798149879a829f103021a0af400021a0a880ef008000c1ad00b21d80c128846f00c00202b88048c099029d1081cf00800090dd70828d20828d80c179819a04eb049f10302090af40002090af00002090af00a00090dd70826d20826a079d10831d40831d00831e00b12f00001092bd80c10d70c28d20c28d10c2ad40c2ad00c2ad00c2ad00c2af00301092bf00001092b'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 5),
    ('INT', 0),
    ('INT', 0),
    ('INT', 8),
    ('INT', 8),
    ('INT', 16777219),
    ('BYTES',
     '97007401000000000000000000006a0100000000000000007c026401ac02a6020000ab0200000000000000007d057c006a02000000000000000072367c00a00300000000000000000000000000000000000000007c03a6010000ab0100000000000000007d067c017c066b02000000007d077401000000000000000000006a0400000000000000007c07a6010000ab0100000000000000007302640353007c057c077a0e00007d057c057c017c036b03000000007a0e00007d057401000000000000000000006a0400000000000000007c05a6010000ab0100000000000000007302640353007c00a00500000000000000000000000000000000000000007c00a00600000000000000000000000000000000000000007c047c017c057c03a6040000ab040000000000000000a6010000ab01000000000000000001007c037c017c053c0000007c055300'),
    ('TUPLE',
     (('STR',
       'Paint within a data region using a boolean mask.\n'
       '\n'
       '        This method operates on the data array in-place, using boolean mask\n'
       '        indexing. It handles preserve_labels logic and optimization to skip\n'
       '        unchanged pixels. The return value is the effective mask of pixels that\n'
       '        were actually painted.\n'
       '\n'
       '        Parameters\n'
       '        ----------\n'
       '        data : np.ndarray\n'
       '            The data to paint (e.g., extracted region/bounding box around polygon).\n'
       '            This array is modified IN PLACE.\n'
       '        mask : np.ndarray\n'
       '            Boolean mask indicating which pixels to paint, with shape matching data.\n'
       '        new_label : int\n'
       '            Label value to paint.\n'
       '        volume_slice : tuple of slice\n'
       '            Slice key in volume space for undo/redo history.\n'
       '\n'
       '        Returns\n'
       '        -------\n'
       '        np.ndarray | None\n'
       '            Boolean mask of the pixels that were painted, or None if the\n'
       '            operation made no changes.\n'
       '\n'
       '        Notes\n'
       '        -----\n'
       '        - Saves to undo/redo history\n'
       "        - Does NOT update caches or refresh display (caller's responsibility)\n"
       '        '),
      ('BOOL', True),
      ('TUPLE', (('STR', 'copy'),)),
      ('NONE',))),
    ('TUPLE',
     (('STR', 'np'),
      ('STR', 'array'),
      ('STR', 'preserve_labels'),
      ('STR', '_get_preserve_labels_source_label'),
      ('STR', 'any'),
      ('STR', '_save_history'),
      ('STR', '_history_atom_for_mask_paint'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'data'),
      ('STR', 'mask'),
      ('STR', 'new_label'),
      ('STR', 'volume_slice'),
      ('STR', 'effective_mask'),
      ('STR', 'source_label'),
      ('STR', 'keep_mask'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_apply_mask_to_data'),
    ('STR', 'Labels._apply_mask_to_data'),
    ('INT', 1845),
    ('BYTES', ''),
    ('BYTES',
     '8000f54a01001a1c9c18a024a854d01932d11932d41932880ee00b0fd40b1ff000050928d81b1fd71b41d21b41c029d11b4cd41b4c884cd8181ca00cd2182c8849dd131594369829d11324d41324f000010d1cd8171b9074d80c1a9869d10c27884ee008169824a029d21a2bd1082b880edd0f118c76906ed10f25d40f25f000010918d813179034e0080cd7081ad2081ad80c10d70c2dd20c2dd8101c9864a04eb049f103020d0ef400020d0ef10304090af40004090af00004090af00a0020298804885ed1081ce00f1dd0081d'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 2),
    ('INT', 0),
    ('INT', 0),
    ('INT', 2),
    ('INT', 2),
    ('INT', 16777219),
    ('BYTES',
     '97007c017c006a0000000000000000006a0100000000000000006b020000000072157c006a02000000000000000081077c006a02000000000000000053007c006a03000000000000000053007c006a0000000000000000006a0100000000000000005300'),
    ('TUPLE',
     (('STR',
       'Return the existing label value that preserve_labels allows to change.\n'
       '\n'
       '        Painting with a non-background label may only replace background.\n'
       '        Painting with the background label may only replace the previously\n'
       '        selected label, falling back to the current selected label when no\n'
       '        previous label is stored.\n'
       '\n'
       '        Parameters\n'
       '        ----------\n'
       '        new_label : int\n'
       '            The label value that will be written.\n'
       '\n'
       '        Returns\n'
       '        -------\n'
       '        int\n'
       '            The only existing label value that may be overwritten.\n'
       '        '),)),
    ('TUPLE',
     (('STR', 'colormap'),
      ('STR', 'background_value'),
      ('STR', '_prev_selected_label'),
      ('STR', 'selected_label'))),
    ('TUPLE', (('STR', 'self'), ('STR', 'new_label'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_get_preserve_labels_source_label'),
    ('STR', 'Labels._get_preserve_labels_source_label'),
    ('INT', 1341),
    ('BYTES', ''),
    ('BYTES',
     '8000f024000c1598049c0dd41836d20b36d00b36d80f13d40f28d00f34d8171bd41730d01030d81317d41326d00c26d80f138c7dd40f2dd0082d'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 5),
    ('INT', 0),
    ('INT', 0),
    ('INT', 11),
    ('INT', 6),
    ('INT', 16777219),
    ('BYTES',
     '97007c006a0000000000000000006a010000000000000000720c7c006a0200000000000000006a0300000000000000007202640153007409000000000000000000007c006a050000000000000000740c000000000000000000006a070000000000000000a6020000ab020000000000000000722b740d000000000000000000006a0800000000000000007c006a0500000000000000007c006a0200000000000000006a0900000000000000006a0a0000000000000000a6020000ab0200000000000000007202640153007c00a00b00000000000000000000000000000000000000007c02a6010000ab0100000000000000007d057c058002640153007c055c0200007d067d077c017419000000000000000000007c06a6010000ab010000000000000000190000000000000000007d087c037419000000000000000000007c06a6010000ab010000000000000000190000000000000000007d097c00a00d00000000000000000000000000000000000000007c087c09a6020000ab0200000000000000005c0200007d087d097409000000000000000000007c006a050000000000000000740c000000000000000000006a070000000000000000a6020000ab0200000000000000007229740d000000000000000000006a0800000000000000007c006a0500000000000000007c006a0200000000000000006a0900000000000000006a0e0000000000000000a6020000ab02000000000000000073217c087c006a0200000000000000006a0900000000000000006a0e00000000000000007419000000000000000000007c07a6010000ab0100000000000000003c0000007c006a0f000000000000000064026b04000000007202640153007c006a100000000000000000a0110000000000000000000000000000000000000000740d000000000000000000006a1200000000000000007c0467017c086a130000000000000000ac03a6020000ab020000000000000000a6010000ab0100000000000000006402190000000000000000007d0a7c09a0140000000000000000000000000000000000000000a6000000ab00000000000000000072237c0a7c006a0200000000000000006a0900000000000000006a0a00000000000000007419000000000000000000007c07a6010000ab0100000000000000003c000000640153007c0a7c006a0200000000000000006a0900000000000000006a0a00000000000000007419000000000000000000007c07a6010000ab010000000000000000190000000000000000007c093c00000064015300'),
    ('TUPLE',
     (('STR',
       'Update raw and view caches with new data from a painted region.\n'
       '\n'
       '        Checks if the painted region intersects with the currently displayed slice.\n'
       '        If so, extracts the visible sub-region and updates the caches. This method\n'
       '        handles painting on non-shared memory backends (zarr, tensorstore, etc.)\n'
       '        that require manual cache updates.\n'
       '\n'
       '        Parameters\n'
       '        ----------\n'
       '        region_data : np.ndarray\n'
       '            The data of the painted region, with full data dimensionality.\n'
       '        slice_key : tuple of slice\n'
       '            The slices used to extract region_data from the full volume;\n'
       '            length-1 slices for non-painted dims.\n'
       '        mask : np.ndarray\n'
       '            Boolean mask indicating modified pixels in region_data.\n'
       '            Used to optimize texture updates.\n'
       '        new_label : int\n'
       '            The new label value that was painted. Used to optimize texture updates.\n'
       '        '),
      ('NONE',),
      ('INT', 0),
      ('TUPLE', (('STR', 'dtype'),)))),
    ('TUPLE',
     (('STR', '_slicing_state'),
      ('STR', 'loaded'),
      ('STR', '_slice'),
      ('STR', 'empty'),
      ('STR', 'isinstance'),
      ('STR', 'data'),
      ('STR', 'np'),
      ('STR', 'ndarray'),
      ('STR', 'shares_memory'),
      ('STR', 'image'),
      ('STR', 'view'),
      ('STR', '_get_update_slices'),
      ('STR', 'tuple'),
      ('STR', '_align_data_to_view'),
      ('STR', 'raw'),
      ('STR', 'contour'),
      ('STR', 'colormap'),
      ('STR', '_data_to_texture'),
      ('STR', 'array'),
      ('STR', 'dtype'),
      ('STR', 'all'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'region_data'),
      ('STR', 'slice_key'),
      ('STR', 'mask'),
      ('STR', 'new_label'),
      ('STR', 'update_slices'),
      ('STR', 'region_slices'),
      ('STR', 'view_slices'),
      ('STR', 'visible_data'),
      ('STR', 'visible_mask'),
      ('STR', 'new_color'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_refresh_caches_from_region'),
    ('STR', 'Labels._refresh_caches_from_region'),
    ('INT', 1927),
    ('BYTES', ''),
    ('BYTES',
     '8000f03c001014d40f22d40f29f000010913a854ac5bd42d3ef000010913d80c128846f50a000c1690649469a512a41ad10b2cd40b2cf000030913b512d43141d80c108c499074947bd41728d4172df10302320af40002320af000030913f006000d138846e0181cd7182fd2182fb009d1183ad4183a880dd80b18d00b20d80c128846e02532d10822880d907bf006001823a535a81dd12337d42337d41738880cd8171b9d45a02dd11c30d41c30d41731880ce02529d7253dd2253dd80c18982cf10302260af40002260ad10822880c906cf50c000d1790749479a522a42ad10c2dd40c2df00304094501e51012d41020a014a419a844ac4bd42c3dd42c41d11042d41042f00504094501f0080039450188448c4bd40c1dd40c21a525a80bd12234d42234d10c35f00a000c108c3c9821d20b1bd00b1bd80c128846f006001519944dd71432d21432dd0c0e8c489069905ba80cd4283ad00c3bd10c3bd40c3bf10302150af40002150ae00a0bf40502150d8809f00a000c18d70b1bd20b1bd10b1dd40b1df00005090ed8394288448c4bd40c1dd40c22a535a81bd12335d42335d10c36d00c36d00c36f00600111af003000d118c4bd40c1dd40c22a535a81bd12335d42335d40c36b07cd10c44d00c44d00c44'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 2),
    ('INT', 0),
    ('INT', 0),
    ('INT', 9),
    ('INT', 6),
    ('INT', 16777219),
    ('BYTES',
     '97007c006a0000000000000000006a0100000000000000007d027c00a0020000000000000000000000000000000000000000a6000000ab0000000000000000007d037407000000000000000000006401a6010000ab01000000000000000067017c006a0400000000000000007a0500007d047407000000000000000000006401a6010000ab0100000000000000006701740b000000000000000000007c02a6010000ab0100000000000000007a0500007d05740d000000000000000000007c006a040000000000000000a6010000ab01000000000000000044005d657d067c017c06190000000000000000007d077c067c02760072197c077c057c02a00700000000000000000000000000000000000000007c06a6010000ab0100000000000000003c0000008c277c037c06190000000000000000007d087c076a0800000000000000007c08630278026b0100000000720b7c076a0900000000000000006b000000000073056e0101000100640153007415000000000000000000007c087c076a0800000000000000007a0a0000a6010000ab0100000000000000007c047c063c0000008c667c047c0566025300'),
    ('TUPLE',
     (('STR',
       'Calculate slices for extracting region data and updating the view.\n'
       '\n'
       '        Returns\n'
       '        -------\n'
       '        tuple[list[slice | int], list[slice]] | None\n'
       '            A tuple (region_slices, view_slices). region_slices extracts\n'
       '            the currently visible part of the painted region (integer\n'
       '            indices collapse the non-displayed dims); view_slices addresses\n'
       '            that part within the displayed slice caches.\n'
       '            Returns None if the painted region is not currently visible.\n'
       '        '),
      ('NONE',))),
    ('TUPLE',
     (('STR', '_slice_input'),
      ('STR', 'displayed'),
      ('STR', '_get_pt_not_disp'),
      ('STR', 'slice'),
      ('STR', 'ndim'),
      ('STR', 'len'),
      ('STR', 'range'),
      ('STR', 'index'),
      ('STR', 'start'),
      ('STR', 'stop'),
      ('STR', 'int'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'slice_key'),
      ('STR', 'displayed_dims'),
      ('STR', 'pt_not_disp'),
      ('STR', 'region_slices'),
      ('STR', 'view_slices'),
      ('STR', 'd'),
      ('STR', 'axis_slice'),
      ('STR', 'current_pos'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_get_update_slices'),
    ('STR', 'Labels._get_update_slices'),
    ('INT', 2008),
    ('BYTES', ''),
    ('BYTES',
     '8000f01c001a1ed4192ad41934880ed8161ad7162bd2162bd1162dd4162d880bf50c002d32b024a94bac4ba83db834bc39d12b44880ddd2429a824a14ba44ba03db533b07ed13346d43346d12346880be5111690749479d11121d41121f00009094701f000090947018841d81922a0319c1c884ad80f10904ed00f22d00f22d83741900b984ed71c30d21c30b011d11c33d41c33d11034d01034f006001f2aa8219c6e900bd81822d41828a84bd01849d01849d21849d01849b82abc2fd21849d01849d01849d01849d81b1f98349834dd2326a07bb05ad43545d12745d12346d42346900d9861d11020d01020e00f1c986bd00f29d00829'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 3),
    ('INT', 0),
    ('INT', 0),
    ('INT', 5),
    ('INT', 4),
    ('INT', 16777219),
    ('BYTES',
     '870597007c006a0000000000000000006a0100000000000000007d037405000000000000000000007c03a6010000ab0100000000000000008a057407000000000000000000007c03a6010000ab01000000000000000089056b0300000000723888056601640184087c034400a6000000ab0000000000000000007d047409000000000000000000006a0500000000000000007c017c04a6020000ab0200000000000000007d017409000000000000000000006a0500000000000000007c027c04a6020000ab0200000000000000007d027c017c0266025300'),
    ('TUPLE',
     (('STR',
       'Transpose data from ascending dimension order to the displayed order.\n'
       '\n'
       '        ``displayed`` may not be sorted (e.g. a transposed view), so the\n'
       '        extracted region (whose axes are in ascending dimension order) is\n'
       '        permuted to match the order the display caches expect.\n'
       '        '),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 5),
        ('INT', 16777235),
        ('BYTES',
         '9501970067007c005d177d018902a00000000000000000000000000000000000000000007c01a6010000ab01000000000000000091028c185300'),
        ('TUPLE', ()),
        ('TUPLE', (('STR', 'index'),)),
        ('TUPLE', (('STR', '.0'), ('STR', 'd'))),
        ('TUPLE', (('STR', 'sorted_dims'),)),
        ('TUPLE', ()),
        ('STR', '<listcomp>'),
        ('STR', 'Labels._align_data_to_view.<locals>.<listcomp>'),
        ('INT', 2059),
        ('BYTES', ''),
        ('BYTES', 'f88000d01341d01341d01341a851904bd71425d21425a061d11428d41428d01341d01341d01341'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))))),
    ('TUPLE',
     (('STR', '_slice_input'),
      ('STR', 'displayed'),
      ('STR', 'sorted'),
      ('STR', 'list'),
      ('STR', 'np'),
      ('STR', 'transpose'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'visible_data'),
      ('STR', 'visible_mask'),
      ('STR', 'displayed_dims'),
      ('STR', 'perm'))),
    ('TUPLE', ()),
    ('TUPLE', (('STR', 'sorted_dims'),)),
    ('STR', '_align_data_to_view'),
    ('STR', 'Labels._align_data_to_view'),
    ('INT', 2045),
    ('BYTES', ''),
    ('BYTES',
     'f88000f016001a1ed4192ad41934880edd161c985ed1162cd4162c880bdd0b0f900ed10b1fd40b1fa03bd20b2ed00b2ed81341d01341d01341d01341b02ed01341d11341d413418844dd1b1d9c3ca80cb064d11b3bd41b3b884cdd1b1d9c3ca80cb064d11b3bd41b3b884cd80f1b985cd00f29d00829'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 1),
    ('INT', 0),
    ('INT', 0),
    ('INT', 2),
    ('INT', 5),
    ('INT', 16777219),
    ('BYTES',
     '870297007c006a0000000000000000006a0100000000000000007d017405000000000000000000006a0300000000000000007c00a00400000000000000000000000000000000000000007c016a0500000000000000006a060000000000000000a6010000ab010000000000000000a6010000ab010000000000000000a0070000000000000000000000000000000000000000741000000000000000000000a6010000ab0100000000000000008a0288026601640184087c016a0900000000000000004400a6000000ab0000000000000000005300'),
    ('TUPLE',
     (('STR', '\n        Get indices of current visible slice.\n        '),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 5),
        ('INT', 16777235),
        ('BYTES', '9501970069007c005d0b7d017c0189027c011900000000000000000093028c0c5300'),
        ('TUPLE', ()),
        ('TUPLE', ()),
        ('TUPLE', (('STR', '.0'), ('STR', 'dim'))),
        ('TUPLE', (('STR', 'point'),)),
        ('TUPLE', ()),
        ('STR', '<dictcomp>'),
        ('STR', 'Labels._get_pt_not_disp.<locals>.<dictcomp>'),
        ('INT', 2084),
        ('BYTES', ''),
        ('BYTES', 'f88000d00f45d00f45d00f45a043900390559833945ad00f45d00f45d00f45'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))))),
    ('TUPLE',
     (('STR', '_slice'),
      ('STR', 'slice_input'),
      ('STR', 'np'),
      ('STR', 'round'),
      ('STR', 'world_to_data'),
      ('STR', 'world_slice'),
      ('STR', 'point'),
      ('STR', 'astype'),
      ('STR', 'int'),
      ('STR', 'not_displayed'))),
    ('TUPLE', (('STR', 'self'), ('STR', 'slice_input'))),
    ('TUPLE', ()),
    ('TUPLE', (('STR', 'point'),)),
    ('STR', '_get_pt_not_disp'),
    ('STR', 'Labels._get_pt_not_disp'),
    ('INT', 2076),
    ('BYTES', ''),
    ('BYTES',
     'f88000f00800171b946bd4162d880bdd10129408d80c10d70c1ed20c1e987bd41f36d41f3cd10c3dd40c3df10302110af40002110ae70a108a269513892b8c2bf00500090ef00600104601d00f45d00f45d00f45a83bd42b44d00f45d10f45d40f45d00845'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 2),
    ('INT', 0),
    ('INT', 0),
    ('INT', 3),
    ('INT', 6),
    ('INT', 16777219),
    ('BYTES',
     '97007c006a00000000000000000064016b000000000072027c01530074030000000000000000000064017405000000000000000000007c006a000000000000000000a6010000ab010000000000000000a6020000ab0200000000000000007d027407000000000000000000007c017c006a0400000000000000006a0500000000000000007c02a6030000ab0300000000000000005300'),
    ('TUPLE',
     (('STR', 'Expand dirty slices so contour refresh includes the full contour halo.'), ('INT', 1))),
    ('TUPLE',
     (('STR', 'contour'),
      ('STR', 'max'),
      ('STR', 'int'),
      ('STR', 'expand_slice'),
      ('STR', 'data'),
      ('STR', 'shape'))),
    ('TUPLE', (('STR', 'self'), ('STR', 'updated_slice'), ('STR', 'contour_offset'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_expand_updated_slice_for_contour'),
    ('STR', 'Labels._expand_updated_slice_for_contour'),
    ('INT', 1742),
    ('BYTES', ''),
    ('BYTES',
     '8000f008000c108c3c9821d20b1bd00b1bd81320d00c20e5191c9851a503a044a44cd12031d42031d11932d41932880edd0f1b984da834ac39ac3fb84ed10f4bd40f4bd0084b'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 2),
    ('INT', 0),
    ('INT', 0),
    ('INT', 2),
    ('INT', 8),
    ('INT', 16777219),
    ('BYTES',
     '97007c00a00000000000000000000000000000000000000000007c01a6010000ab0100000000000000007d017c006a01000000000000000080097c017c005f01000000000000000064015300740500000000000000000000640284007407000000000000000000007c006a0100000000000000007c016403ac04a6030000ab0300000000000000004400a6000000ab000000000000000000a6010000ab0100000000000000007c005f01000000000000000064015300'),
    ('TUPLE',
     (('STR', 'Merge a newly dirtied region into the pending partial refresh.'),
      ('NONE',),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 3),
        ('INT', 8),
        ('INT', 16777267),
        ('BYTES',
         '4b00010097007c005d475c0200007d017d027401000000000000000000007403000000000000000000007c016a0200000000000000007c026a020000000000000000a6020000ab0200000000000000007407000000000000000000007c016a0400000000000000007c026a040000000000000000a6020000ab020000000000000000a6020000ab0200000000000000005600970101008c4864005300'),
        ('TUPLE', (('NONE',),)),
        ('TUPLE', (('STR', 'slice'), ('STR', 'min'), ('STR', 'start'), ('STR', 'max'), ('STR', 'stop'))),
        ('TUPLE', (('STR', '.0'), ('STR', 's1'), ('STR', 's2'))),
        ('TUPLE', ()),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'Labels._accumulate_updated_slice.<locals>.<genexpr>'),
        ('INT', 1762),
        ('BYTES', ''),
        ('BYTES',
         'e800e8008000f00003240af00003240ae1101690029042f503000d12952390629468a002a408d11229d41229ad33a872ac77b802bc07d12b40d42b40d10c41d40c41f00303240af00003240af00003240af00003240af00003240af00003240a'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))),
      ('BOOL', True),
      ('TUPLE', (('STR', 'strict'),)))),
    ('TUPLE',
     (('STR', '_expand_updated_slice_for_contour'),
      ('STR', '_updated_slice'),
      ('STR', 'tuple'),
      ('STR', 'zip'))),
    ('TUPLE', (('STR', 'self'), ('STR', 'updated_slice'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_accumulate_updated_slice'),
    ('STR', 'Labels._accumulate_updated_slice'),
    ('INT', 1752),
    ('BYTES', ''),
    ('BYTES',
     '8000f00800191dd7183ed2183eb87dd1184dd4184d880de00b0fd40b1ed00b26d8222f8844d40c1fd80c128846e51e23f00003240af00003240ae51a1d9864d41e31b03dc814d01a4ed11a4ed41a4ef00503240af10003240af40003240af100031f0af400031f0a8804d4081bd0081bd0081b'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 1),
    ('INT', 0),
    ('INT', 0),
    ('INT', 6),
    ('INT', 4),
    ('INT', 16777219),
    ('BYTES',
     '8700970089006a000000000000000000810c89006a0100000000000000006a02000000000000000073026401530089006a0300000000000000006a0400000000000000007d0189006a0500000000000000006a0600000000000000006a0700000000000000007d0274110000000000000000000088006601640284087c014400a6000000ab000000000000000000a6010000ab0100000000000000007d03640384007c034400a6000000ab0000000000000000007d0489006a09000000000000000064046b040000000072188900a00a00000000000000000000000000000000000000007c027c03ac05a6020000ab0200000000000000007d056e1789006a0500000000000000006a0600000000000000006a0b00000000000000007c03190000000000000000007d057c0589006a0500000000000000006a0600000000000000006a0b00000000000000007c033c00000089006a0c0000000000000000a00d00000000000000000000000000000000000000007c057c04ac06a6020000ab0200000000000000000100640189005f00000000000000000064015300'),
    ('TUPLE',
     (('STR', 'Prepares and displays only an updated part of the labels.'),
      ('NONE',),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 3),
        ('INT', 16777267),
        ('BYTES',
         '95014b00010097007c005d117d0189026a0000000000000000007c01190000000000000000005600970101008c1264005300'),
        ('TUPLE', (('NONE',),)),
        ('TUPLE', (('STR', '_updated_slice'),)),
        ('TUPLE', (('STR', '.0'), ('STR', 'index'))),
        ('TUPLE', (('STR', 'self'),)),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'Labels._partial_labels_refresh.<locals>.<genexpr>'),
        ('INT', 941),
        ('BYTES', ''),
        ('BYTES',
         'f8e800e8008000f000021e0af000021e0ad82b308844d40c1fa005d40c26f003021e0af000021e0af000021e0af000021e0af000021e0af000021e0a'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 3),
        ('INT', 16777235),
        ('BYTES', '970067007c005d097d017c016a00000000000000000091028c0a5300'),
        ('TUPLE', ()),
        ('TUPLE', (('STR', 'start'),)),
        ('TUPLE', (('STR', '.0'), ('STR', 'axis_slice'))),
        ('TUPLE', ()),
        ('TUPLE', ()),
        ('STR', '<listcomp>'),
        ('STR', 'Labels._partial_labels_refresh.<locals>.<listcomp>'),
        ('INT', 945),
        ('BYTES', ''),
        ('BYTES', '8000d01143d01143d01143a07a902ad41222d01143d01143d01143'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))),
      ('INT', 0),
      ('TUPLE', (('STR', 'data_slice'),)),
      ('TUPLE', (('STR', 'data'), ('STR', 'offset'))))),
    ('TUPLE',
     (('STR', '_updated_slice'),
      ('STR', '_slicing_state'),
      ('STR', 'loaded'),
      ('STR', '_slice_input'),
      ('STR', 'displayed'),
      ('STR', '_slice'),
      ('STR', 'image'),
      ('STR', 'raw'),
      ('STR', 'tuple'),
      ('STR', 'contour'),
      ('STR', '_raw_to_displayed'),
      ('STR', 'view'),
      ('STR', 'events'),
      ('STR', 'labels_update'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'dims_displayed'),
      ('STR', 'raw_displayed'),
      ('STR', 'updated_slice'),
      ('STR', 'offset'),
      ('STR', 'colors_sliced'))),
    ('TUPLE', ()),
    ('TUPLE', (('STR', 'self'),)),
    ('STR', '_partial_labels_refresh'),
    ('STR', 'Labels._partial_labels_refresh'),
    ('INT', 931),
    ('BYTES', ''),
    ('BYTES',
     'f88000f006000c10d40b1ed00b26a864d42e41d42e48d00b26d80c128846e0191dd4192ad41934880ed8181c9c0bd41829d4182d880df50600191ef000021e0af000021e0af000021e0af000021e0ad83442f003021e0af100021e0af400021e0af10002190af40002190a880df00800124401d01143b05dd01143d11143d411438806e00b0f8c3c9821d20b1bd00b1bd81c20d71c32d21c32d8101da82df003001d33f100021d0ef400021d0e884d884df008001d219c4bd41c2dd41c32b03dd41c41884df00e00313e88048c0bd40819d4081e987dd1082de0080c8c0bd70821d20821a07db856d00821d10844d40844d00844d81e228804d4081bd0081bd0081b'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 4),
    ('INT', 0),
    ('INT', 0),
    ('INT', 8),
    ('INT', 7),
    ('INT', 16777219),
    ('BYTES',
     '870897007c006a0000000000000000007c01190000000000000000007c026b03000000008a0874030000000000000000000088086601640184087c014400a6000000ab000000000000000000a6010000ab0100000000000000007d017405000000000000000000007c02740600000000000000000000a6020000ab020000000000000000722b7409000000000000000000006a0500000000000000007c027c006a0600000000000000006a0700000000000000006a0800000000000000006a090000000000000000ac02a6020000ab0200000000000000007d026e297c006a0600000000000000006a0700000000000000006a0800000000000000006a090000000000000000a00a00000000000000000000000000000000000000007c02a6010000ab0100000000000000007d027405000000000000000000007c027408000000000000000000006a0b0000000000000000a6020000ab02000000000000000072087c028908190000000000000000007d027c0172117c016403190000000000000000006a0c000000000000000064036b02000000007202640453007c00a00d00000000000000000000000000000000000000007c017409000000000000000000006a0e00000000000000007c006a0000000000000000007c01190000000000000000006405ac06a6020000ab0200000000000000007c026603a6010000ab01000000000000000001007c027c006a0000000000000000007c013c0000007c00a00f0000000000000000000000000000000000000000a6000000ab0000000000000000007d047421000000000000000000007c017c047c006a0600000000000000006a1100000000000000006a120000000000000000a6030000ab0300000000000000007d057405000000000000000000007c027408000000000000000000006a0b0000000000000000a6020000ab02000000000000000072177c027427000000000000000000007c017c04a6020000ab020000000000000000190000000000000000007d066e027c027d067405000000000000000000007c006a0000000000000000007408000000000000000000006a0b0000000000000000a6020000ab02000000000000000073427405000000000000000000007429000000000000000000007c006a00000000000000000064076404a6030000ab0300000000000000007408000000000000000000006a0b0000000000000000a6020000ab02000000000000000073147c067c006a0600000000000000006a0700000000000000006a0800000000000000007c053c0000007405000000000000000000007c006a0000000000000000007408000000000000000000006a0b0000000000000000a6020000ab020000000000000000730c640884007c014400a6000000ab0000000000000000007d01740300000000000000000000640984007c014400a6000000ab000000000000000000a6010000ab0100000000000000007d077c006a15000000000000000064036b0200000000722c7c006a160000000000000000a01700000000000000000000000000000000000000007c06a6010000ab0100000000000000007c006a0600000000000000006a0700000000000000006a1800000000000000007c053c0000007c00a01900000000000000000000000000000000000000007c07a6010000ab01000000000000000001007c036405750072167c00a01a0000000000000000000000000000000000000000a6000000ab00000000000000000001006404530064045300'),
    ('TUPLE',
     (('STR',
       'Set `indices` in `data` to `value`, while writing to edit history.\n'
       '\n'
       '        Parameters\n'
       '        ----------\n'
       '        indices : tuple of arrays of int\n'
       '            Indices in data to overwrite. Must be a tuple of arrays of length\n'
       '            equal to the number of data dimensions. (Fancy indexing in [2]_).\n'
       '        value : int or array of int\n'
       '            New label value(s). If more than one value, must match or\n'
       '            broadcast with the given indices.\n'
       '        refresh : bool, default True\n'
       '            whether to refresh the view, by default True\n'
       '\n'
       '        References\n'
       '        ----------\n'
       '        .. [2] https://numpy.org/doc/stable/user/basics.indexing.html\n'
       '        '),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 3),
        ('INT', 16777267),
        ('BYTES', '95014b00010097007c005d0c7d017c018902190000000000000000005600970101008c0d64005300'),
        ('TUPLE', (('NONE',),)),
        ('TUPLE', ()),
        ('TUPLE', (('STR', '.0'), ('STR', 'x'))),
        ('TUPLE', (('STR', 'changed_indices'),)),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'Labels.data_setitem.<locals>.<genexpr>'),
        ('INT', 2105),
        ('BYTES', ''),
        ('BYTES', 'f8e800e8008000d0173cd0173ca8719801982fd4182ad0173cd0173cd0173cd0173cd0173cd0173c'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))),
      ('TUPLE', (('STR', 'dtype'),)),
      ('INT', 0),
      ('NONE',),
      ('BOOL', True),
      ('TUPLE', (('STR', 'copy'),)),
      ('STR', 'data'),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 5),
        ('INT', 16777235),
        ('BYTES',
         '970067007c005d287d017401000000000000000000006a0100000000000000007c01a6010000ab010000000000000000a0020000000000000000000000000000000000000000a6000000ab00000000000000000091028c295300'),
        ('TUPLE', ()),
        ('TUPLE', (('STR', 'np'), ('STR', 'array'), ('STR', 'flatten'))),
        ('TUPLE', (('STR', '.0'), ('STR', 'x'))),
        ('TUPLE', ()),
        ('TUPLE', ()),
        ('STR', '<listcomp>'),
        ('STR', 'Labels.data_setitem.<locals>.<listcomp>'),
        ('INT', 2153),
        ('BYTES', ''),
        ('BYTES', '8000d0163ed0163ed0163eb01195729478a001917b947bd7172ad2172ad1172cd4172cd0163ed0163ed0163e'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 8),
        ('INT', 16777267),
        ('BYTES',
         '4b00010097007c005d557d017401000000000000000000007403000000000000000000007c01a0020000000000000000000000000000000000000000a6000000ab000000000000000000a6010000ab0100000000000000007403000000000000000000007c01a0030000000000000000000000000000000000000000a6000000ab000000000000000000a6010000ab01000000000000000064007a000000a6020000ab0200000000000000005600970101008c5664015300'),
        ('TUPLE', (('INT', 1), ('NONE',))),
        ('TUPLE', (('STR', 'slice'), ('STR', 'int'), ('STR', 'min'), ('STR', 'max'))),
        ('TUPLE', (('STR', '.0'), ('STR', 'axis_indices'))),
        ('TUPLE', ()),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'Labels.data_setitem.<locals>.<genexpr>'),
        ('INT', 2155),
        ('BYTES', ''),
        ('BYTES',
         'e800e8008000f000031e0af000031e0ae0101cf503000d129523906cd71626d21626d11628d41628d11229d41229ad33a87cd72f3fd22f3fd12f41d42f41d12b42d42b42c051d12b46d10c47d40c47f003031e0af000031e0af000031e0af000031e0af000031e0af000031e0a'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))))),
    ('TUPLE',
     (('STR', 'data'),
      ('STR', 'tuple'),
      ('STR', 'isinstance'),
      ('STR', 'Sequence'),
      ('STR', 'np'),
      ('STR', 'asarray'),
      ('STR', '_slice'),
      ('STR', 'image'),
      ('STR', 'raw'),
      ('STR', 'dtype'),
      ('STR', 'type'),
      ('STR', 'ndarray'),
      ('STR', 'size'),
      ('STR', '_save_history'),
      ('STR', 'array'),
      ('STR', '_get_pt_not_disp'),
      ('STR', 'index_in_slice'),
      ('STR', 'slice_input'),
      ('STR', 'order'),
      ('STR', 'elements_in_slice'),
      ('STR', 'getattr'),
      ('STR', 'contour'),
      ('STR', 'colormap'),
      ('STR', '_data_to_texture'),
      ('STR', 'view'),
      ('STR', '_accumulate_updated_slice'),
      ('STR', '_partial_labels_refresh'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'indices'),
      ('STR', 'value'),
      ('STR', 'refresh'),
      ('STR', 'pt_not_disp'),
      ('STR', 'displayed_indices'),
      ('STR', 'visible_values'),
      ('STR', 'updated_slice'))),
    ('TUPLE', ()),
    ('TUPLE', (('STR', 'changed_indices'),)),
    ('STR', 'data_setitem'),
    ('STR', 'Labels.data_setitem'),
    ('INT', 2086),
    ('BYTES', ''),
    ('BYTES',
     'f88000f024001b1f9c29a047d41a2cb005d21a35880fdd1217d0173cd0173cd0173cd0173cb047d0173cd1173cd4173cd1123cd4123c8807e50b1590659d58d10b26d40b26f00003093cdd1416944a9875a844ac4bd42c3dd42c41d42c47d01448d11448d4144888458845e01418944bd41425d41429d4142fd71434d21434b055d1143bd4143b8845f506000c1690659d529c5ad10b28d40b28f00001092bd81419982fd4142a8845e00f16f0000109139827a0219c2a9c2fa851d21a2ed01a2ed80c128846e0080cd7081ad2081ae01017dd1012940898149c19a037d4192bb024d01037d11037d41037d81015f007040d0ef10306090af40006090af00006090af012001e2388048c099027d1081ae0161ad7162bd2162bd1162dd4162d880bdd1c2ad80c13905ba024a42bd42239d4223ff103021d0af400021d0ad00819f506000c1690659d529c5ad10b28d40b28f000030923d81d22d52334b057b86bd1234ad4234ad41d4b884e884ee01d22884ef506000d1790749479a522a42ad10c2dd40c2df00309094601e50f199d27a024a429a856b054d11a3ad41a3abd42bc4ad10f47d40f47f00509094601f0120038460188448c4bd40c1dd40c21d02233d10c34f50800101a98249c29a552a45ad10f30d40f30f00001093fd8163ed0163eb067d0163ed1163ed4163e8847e5181df000031e0af000031e0ae02027f005031e0af100031e0af400031e0af10003190af40003190a880df00a000c108c3c9831d20b1cd00b1cf006001115940dd7102ed2102ea87ed1103ed4103ef003000d118c4bd40c1dd40c22d02334d10c35f00800090dd70826d20826a07dd10835d40835d00835e00b129064883f883fd80c10d70c28d20c28d10c2ad40c2ad00c2ad00c2ad00c2af003000c1b883f'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 2),
    ('INT', 0),
    ('INT', 0),
    ('INT', 3),
    ('INT', 4),
    ('INT', 3),
    ('BYTES',
     '8700970088006601640184087c01a0000000000000000000000000000000000000000000a6000000ab0000000000000000004400a6000000ab0000000000000000007d027402000000000000000000006a020000000000000000a00300000000000000000000000000000000000000007c026402ac03a6020000ab0200000000000000005300'),
    ('TUPLE',
     (('STR',
       'Mask elements from a multi-dimensional index not in a given slice.\n'
       '\n'
       '    Some n-D operations may edit data that is not visible in the current slice.\n'
       '    Given slice position information (as a dictionary mapping axis to index on that\n'
       '    axis), this function returns a boolean mask for the possibly higher-dimensional\n'
       '    multi-index so that elements not currently visible are masked out. The\n'
       '    resulting multi-index can then be subset and used to index into a texture or\n'
       '    other lower-dimensional view.\n'
       '\n'
       '    Parameters\n'
       '    ----------\n'
       '    index : tuple of array of int\n'
       '        A NumPy fancy indexing expression [1]_.\n'
       '    position_in_axes : dict[int, int]\n'
       '        A dictionary mapping sliced (non-displayed) axes to a slice position.\n'
       '\n'
       '    Returns\n'
       '    -------\n'
       '    visible : array of bool\n'
       '        A boolean array indicating which items are visible in the current view.\n'
       '    '),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 3),
        ('INT', 4),
        ('INT', 19),
        ('BYTES',
         '9501970067007c005d115c0200007d017d0289037c01190000000000000000007c026b020000000091028c125300'),
        ('TUPLE', ()),
        ('TUPLE', ()),
        ('TUPLE', (('STR', '.0'), ('STR', 'ax'), ('STR', 'position'))),
        ('TUPLE', (('STR', 'index'),)),
        ('TUPLE', ()),
        ('STR', '<listcomp>'),
        ('STR', 'elements_in_slice.<locals>.<listcomp>'),
        ('INT', 29),
        ('BYTES', ''),
        ('BYTES',
         'f88000f000020f06f000020f06f000020f06d9222ea022a068880588628c099058d2081df003020f06f000020f06f000020f06'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\utils\\_indexing.py'))),
      ('INT', 0),
      ('TUPLE', (('STR', 'axis'),)))),
    ('TUPLE', (('STR', 'items'), ('STR', 'np'), ('STR', 'logical_and'), ('STR', 'reduce'))),
    ('TUPLE', (('STR', 'index'), ('STR', 'position_in_axes'), ('STR', 'queries'))),
    ('TUPLE', ()),
    ('TUPLE', (('STR', 'index'),)),
    ('STR', 'elements_in_slice'),
    ('STR', 'elements_in_slice'),
    ('INT', 5),
    ('BYTES', ''),
    ('BYTES',
     'f88000f030020f06f000020f06f000020f06f000020f06d83242d73248d23248d1324ad4324af003020f06f100020f06f400020f068047f506000c0e8c3ed70b20d20b20a017a871d00b20d10b31d40b31d00431'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\utils\\_indexing.py')),
   (('INT', 3),
    ('INT', 0),
    ('INT', 0),
    ('INT', 3),
    ('INT', 5),
    ('INT', 3),
    ('BYTES',
     '870087018703970074010000000000000000000089008901a6020000ab0200000000000000008a037403000000000000000000008800880388016603640184087c024400a6000000ab000000000000000000a6010000ab0100000000000000005300'),
    ('TUPLE',
     (('STR',
       'Convert a NumPy fancy indexing expression from data to sliced space.\n'
       '\n'
       '    Parameters\n'
       '    ----------\n'
       '    index : tuple of array of int\n'
       '        A NumPy fancy indexing expression [1]_.\n'
       '    position_in_axes : dict[int, int]\n'
       '        A dictionary mapping sliced (non-displayed) axes to a slice position.\n'
       '    indices_order : tuple of int\n'
       '        The order of the indices in data view.\n'
       '\n'
       '    Returns\n'
       '    -------\n'
       '    sliced_index : tuple of array of int\n'
       '        The indexing expression (nD) restricted to the current slice (usually\n'
       '        2D or 3D).\n'
       '\n'
       '    Examples\n'
       '    --------\n'
       '    >>> index = (np.arange(5), np.full(5, 1), np.arange(4, 9))\n'
       '    >>> index_in_slice(index, {0: 3}, (0, 1, 2))\n'
       '    (array([1]), array([7]))\n'
       '    >>> index_in_slice(index, {1: 1, 2: 8}, (0, 1, 2))\n'
       '    (array([4]),)\n'
       '\n'
       '    References\n'
       '    ----------\n'
       '    [1]: https://numpy.org/doc/stable/user/basics.indexing.html#integer-array-indexing\n'
       '    '),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 3),
        ('INT', 51),
        ('BYTES',
         '95034b00010097007c005d167d017c0189047601af0689027c01190000000000000000008903190000000000000000005600970101008c1764005300'),
        ('TUPLE', (('NONE',),)),
        ('TUPLE', ()),
        ('TUPLE', (('STR', '.0'), ('STR', 'i'))),
        ('TUPLE', (('STR', 'index'), ('STR', 'index_in_slice'), ('STR', 'position_in_axes'))),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'index_in_slice.<locals>.<genexpr>'),
        ('INT', 70),
        ('BYTES', ''),
        ('BYTES',
         'f8e800e8008000f000041106f000041106e00c0dd80b0cd01424d00b24d00b24f00500090e88618c08901ed40820e00b24d00b24d00b24d00b24f007041106f000041106'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\utils\\_indexing.py'))))),
    ('TUPLE', (('STR', 'elements_in_slice'), ('STR', 'tuple'))),
    ('TUPLE', (('STR', 'index'), ('STR', 'position_in_axes'), ('STR', 'indices_order'))),
    ('TUPLE', ()),
    ('TUPLE', (('STR', 'index'), ('STR', 'position_in_axes'), ('STR', 'index_in_slice'))),
    ('STR', 'index_in_slice'),
    ('STR', 'index_in_slice'),
    ('INT', 35),
    ('BYTES', ''),
    ('BYTES',
     'f8f8f88000f54401001627a075d02e3ed1153fd4153f804edd0b10f000041106f000041106f000041106f000041106f000041106f000041106e0111ef005041106f100041106f400041106f100040c06f400040c06f000040506'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\utils\\_indexing.py')))),
 (12,
  ((('INT', 7),
    ('INT', 0),
    ('INT', 0),
    ('INT', 9),
    ('INT', 6),
    ('INT', 16777219),
    ('BYTES',
     '87049700740100000000000000008804660164018408740300000000000000007c006a04000000000000000000000000000000000000ab010000000000004400ab00000000000000ab010000000000007d077c07722e740700000000000000006a080000000000000000000000000000000000007c027c07ab020000000000007d027c068116740700000000000000006a080000000000000000000000000000000000007c067c07ab020000000000007d067c068022740700000000000000006a0a0000000000000000000000000000000000007c006a0c0000000000000000000000000000000000007c0119000000ab010000000000007d067c006a0f0000000000000000000000000000000000007c067c027c037c01ab040000000000007d087c08800179027c067c006a0c0000000000000000000000000000000000007c013c0000007c006a110000000000000000000000000000000000007c067c017c087c03ab0400000000000001007c006a130000000000000000000000000000000000007c01ab0100000000000001007c0572117c006a15000000000000000000000000000000000000ab00000000000000010079027902'),
    ('TUPLE',
     (('STR',
       'Universal painting method using a boolean mask within a bounding box.\n'
       '\n'
       '        This method extracts the common pattern shared by brush painting, fill\n'
       '        and polygon painting: extract a region, apply a mask, write back, update\n'
       '        caches. It manages cache updates for non-shared memory backends, respects\n'
       '        the preserve_labels setting (handled by _apply_mask_to_data), and updates\n'
       '        undo/redo history.\n'
       '\n'
       '        Parameters\n'
       '        ----------\n'
       '        slice_key : tuple of slice\n'
       '            N-dimensional slice key in volume space specifying the bounding box\n'
       '            region; length-1 slices for non-painted dims. Example:\n'
       '            (slice(5, 6), slice(100, 200), slice(150, 250)) for a 3D volume\n'
       '            where dims_to_paint=[1, 2].\n'
       '        mask : ndarray\n'
       '            Boolean mask indicating which pixels to paint within the bounding\n'
       '            box, with one axis per painted dimension.\n'
       '        new_label : int\n'
       '            Label value to paint.\n'
       '        dims_to_paint : list of int\n'
       '            Indices of dimensions being painted (e.g., [1, 2] for YX in a ZYX volume).\n'
       '        refresh : bool, optional\n'
       '            Whether to refresh the display after painting. Default is True.\n'
       '        region_data : np.ndarray | None, optional\n'
       '            Pre-loaded data for the region defined by slice_key, with one axis\n'
       '            per painted dimension. If None (default), data will be read from\n'
       '            self.data[slice_key]. Providing this avoids redundant reads when\n'
       '            data is already available (e.g. in fill).\n'
       '        '),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 3),
        ('INT', 16777267),
        ('BYTES', '95014b00010097007c005d0b00007d017c018902760173018c087c019601970101008c0d04007900ad037701'),
        ('TUPLE', (('NONE',),)),
        ('TUPLE', ()),
        ('TUPLE', (('STR', '.0'), ('STR', 'dim'))),
        ('TUPLE', (('STR', 'dims_to_paint'),)),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'Labels._paint_region_with_mask.<locals>.<genexpr>'),
        ('INT', 1809),
        ('BYTES', '830914018d071401'),
        ('BYTES', 'f8e800f88000f000021b0ad91b2b9043a873b82dd22f478c43d11b2bf9'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))),
      ('NONE',))),
    ('TUPLE',
     (('STR', 'tuple'),
      ('STR', 'range'),
      ('STR', 'ndim'),
      ('STR', 'np'),
      ('STR', 'expand_dims'),
      ('STR', 'asarray'),
      ('STR', 'data'),
      ('STR', '_apply_mask_to_data'),
      ('STR', '_refresh_caches_from_region'),
      ('STR', '_accumulate_updated_slice'),
      ('STR', '_partial_labels_refresh'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'slice_key'),
      ('STR', 'mask'),
      ('STR', 'new_label'),
      ('STR', 'dims_to_paint'),
      ('STR', 'refresh'),
      ('STR', 'region_data'),
      ('STR', 'extra_axes'),
      ('STR', 'effective_mask'))),
    ('TUPLE', ()),
    ('TUPLE', (('STR', 'dims_to_paint'),)),
    ('STR', '_paint_region_with_mask'),
    ('STR', 'Labels._paint_region_with_mask'),
    ('INT', 1767),
    ('BYTES', ''),
    ('BYTES',
     'f88000f4540100161bf300021b0adc1b20a014a719a119d41b2bf303021b0af30002160a880af106000c16dc1315973e913ea024a80ad313338844d80f1ad00f26dc1e209f6e996ea85bb82ad31e45900be00b16d00b1edc1a1c9f2a992aa054a759a159a879d12539d31a3a884be0191dd71931d11931d80c1798149879a829f303021a0a880ef008000c1ad00b21d80c12f00c00202b88048f0989099029d1081cf00800090dd70828d10828d80c179819a04eb049f40302090af00a00090dd70826d10826a079d40831e10b12d80c10d70c28d10c28d50c2af003000c13'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 5),
    ('INT', 0),
    ('INT', 0),
    ('INT', 8),
    ('INT', 8),
    ('INT', 16777219),
    ('BYTES',
     '9700740100000000000000006a020000000000000000000000000000000000007c026401ac02ab020000000000007d057c006a0400000000000000000000000000000000000072317c006a070000000000000000000000000000000000007c03ab010000000000007d067c017c066b2800007d07740100000000000000006a080000000000000000000000000000000000007c07ab01000000000000730179037c057c077a0e00007d057c057c017c036b3700007a0e00007d05740100000000000000006a080000000000000000000000000000000000007c05ab01000000000000730179037c006a0b0000000000000000000000000000000000007c006a0d0000000000000000000000000000000000007c047c017c057c03ab04000000000000ab0100000000000001007c037c017c053c0000007c055300'),
    ('TUPLE',
     (('STR',
       'Paint within a data region using a boolean mask.\n'
       '\n'
       '        This method operates on the data array in-place, using boolean mask\n'
       '        indexing. It handles preserve_labels logic and optimization to skip\n'
       '        unchanged pixels. The return value is the effective mask of pixels that\n'
       '        were actually painted.\n'
       '\n'
       '        Parameters\n'
       '        ----------\n'
       '        data : np.ndarray\n'
       '            The data to paint (e.g., extracted region/bounding box around polygon).\n'
       '            This array is modified IN PLACE.\n'
       '        mask : np.ndarray\n'
       '            Boolean mask indicating which pixels to paint, with shape matching data.\n'
       '        new_label : int\n'
       '            Label value to paint.\n'
       '        volume_slice : tuple of slice\n'
       '            Slice key in volume space for undo/redo history.\n'
       '\n'
       '        Returns\n'
       '        -------\n'
       '        np.ndarray | None\n'
       '            Boolean mask of the pixels that were painted, or None if the\n'
       '            operation made no changes.\n'
       '\n'
       '        Notes\n'
       '        -----\n'
       '        - Saves to undo/redo history\n'
       "        - Does NOT update caches or refresh display (caller's responsibility)\n"
       '        '),
      ('BOOL', True),
      ('TUPLE', (('STR', 'copy'),)),
      ('NONE',))),
    ('TUPLE',
     (('STR', 'np'),
      ('STR', 'array'),
      ('STR', 'preserve_labels'),
      ('STR', '_get_preserve_labels_source_label'),
      ('STR', 'any'),
      ('STR', '_save_history'),
      ('STR', '_history_atom_for_mask_paint'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'data'),
      ('STR', 'mask'),
      ('STR', 'new_label'),
      ('STR', 'volume_slice'),
      ('STR', 'effective_mask'),
      ('STR', 'source_label'),
      ('STR', 'keep_mask'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_apply_mask_to_data'),
    ('STR', 'Labels._apply_mask_to_data'),
    ('INT', 1845),
    ('BYTES', ''),
    ('BYTES',
     '8000f44a01001a1c9f189918a024a854d41932880ee00b0fd70b1fd20b1fd81b1fd71b41d11b41c029d31b4c884cd8181ca00cd1182c8849dc1315973691369829d41324d8171bd80c1a9869d10c27884ee008169824a029d11a2bd1082b880edc0f118f768976906ed40f25d81317e0080cd7081ad1081ad80c10d70c2dd10c2dd8101c9864a04eb049f303020d0ef40304090af00a0020298804885ed1081ce00f1dd0081d'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 2),
    ('INT', 0),
    ('INT', 0),
    ('INT', 2),
    ('INT', 2),
    ('INT', 16777219),
    ('BYTES',
     '97007c017c006a000000000000000000000000000000000000006a020000000000000000000000000000000000006b28000072247c006a04000000000000000000000000000000000000810c7c006a0400000000000000000000000000000000000053007c006a0600000000000000000000000000000000000053007c006a000000000000000000000000000000000000006a020000000000000000000000000000000000005300'),
    ('TUPLE',
     (('STR',
       'Return the existing label value that preserve_labels allows to change.\n'
       '\n'
       '        Painting with a non-background label may only replace background.\n'
       '        Painting with the background label may only replace the previously\n'
       '        selected label, falling back to the current selected label when no\n'
       '        previous label is stored.\n'
       '\n'
       '        Parameters\n'
       '        ----------\n'
       '        new_label : int\n'
       '            The label value that will be written.\n'
       '\n'
       '        Returns\n'
       '        -------\n'
       '        int\n'
       '            The only existing label value that may be overwritten.\n'
       '        '),)),
    ('TUPLE',
     (('STR', 'colormap'),
      ('STR', 'background_value'),
      ('STR', '_prev_selected_label'),
      ('STR', 'selected_label'))),
    ('TUPLE', (('STR', 'self'), ('STR', 'new_label'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_get_preserve_labels_source_label'),
    ('STR', 'Labels._get_preserve_labels_source_label'),
    ('INT', 1341),
    ('BYTES', ''),
    ('BYTES',
     '8000f024000c1598049f0d990dd71836d11836d20b36d80f13d70f28d10f28d00f34d8171bd71730d11730d01030d81317d71326d11326d00c26d80f138f7d897dd70f2dd10f2dd0082d'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 5),
    ('INT', 0),
    ('INT', 0),
    ('INT', 11),
    ('INT', 6),
    ('INT', 16777219),
    ('BYTES',
     '97007c006a000000000000000000000000000000000000006a0200000000000000000000000000000000000072167c006a040000000000000000000000000000000000006a0600000000000000000000000000000000000072017901740900000000000000007c006a0a000000000000000000000000000000000000740c00000000000000006a0e000000000000000000000000000000000000ab02000000000000723f740d00000000000000006a100000000000000000000000000000000000007c006a0a0000000000000000000000000000000000007c006a040000000000000000000000000000000000006a120000000000000000000000000000000000006a14000000000000000000000000000000000000ab02000000000000720179017c006a170000000000000000000000000000000000007c02ab010000000000007d057c05800179017c055c0200007d067d077c01741900000000000000007c06ab01000000000000190000007d087c03741900000000000000007c06ab01000000000000190000007d097c006a1b0000000000000000000000000000000000007c087c09ab020000000000005c0200007d087d09740900000000000000007c006a0a000000000000000000000000000000000000740c00000000000000006a0e000000000000000000000000000000000000ab02000000000000723e740d00000000000000006a100000000000000000000000000000000000007c006a0a0000000000000000000000000000000000007c006a040000000000000000000000000000000000006a120000000000000000000000000000000000006a1c000000000000000000000000000000000000ab02000000000000732c7c087c006a040000000000000000000000000000000000006a120000000000000000000000000000000000006a1c000000000000000000000000000000000000741900000000000000007c07ab010000000000003c0000007c006a1e00000000000000000000000000000000000064026b440000720179017c006a200000000000000000000000000000000000006a23000000000000000000000000000000000000740d00000000000000006a240000000000000000000000000000000000007c0467017c086a26000000000000000000000000000000000000ac03ab02000000000000ab010000000000006402190000007d0a7c096a29000000000000000000000000000000000000ab00000000000000722d7c0a7c006a040000000000000000000000000000000000006a120000000000000000000000000000000000006a14000000000000000000000000000000000000741900000000000000007c07ab010000000000003c00000079017c0a7c006a040000000000000000000000000000000000006a120000000000000000000000000000000000006a14000000000000000000000000000000000000741900000000000000007c07ab01000000000000190000007c093c0000007901'),
    ('TUPLE',
     (('STR',
       'Update raw and view caches with new data from a painted region.\n'
       '\n'
       '        Checks if the painted region intersects with the currently displayed slice.\n'
       '        If so, extracts the visible sub-region and updates the caches. This method\n'
       '        handles painting on non-shared memory backends (zarr, tensorstore, etc.)\n'
       '        that require manual cache updates.\n'
       '\n'
       '        Parameters\n'
       '        ----------\n'
       '        region_data : np.ndarray\n'
       '            The data of the painted region, with full data dimensionality.\n'
       '        slice_key : tuple of slice\n'
       '            The slices used to extract region_data from the full volume;\n'
       '            length-1 slices for non-painted dims.\n'
       '        mask : np.ndarray\n'
       '            Boolean mask indicating modified pixels in region_data.\n'
       '            Used to optimize texture updates.\n'
       '        new_label : int\n'
       '            The new label value that was painted. Used to optimize texture updates.\n'
       '        '),
      ('NONE',),
      ('INT', 0),
      ('TUPLE', (('STR', 'dtype'),)))),
    ('TUPLE',
     (('STR', '_slicing_state'),
      ('STR', 'loaded'),
      ('STR', '_slice'),
      ('STR', 'empty'),
      ('STR', 'isinstance'),
      ('STR', 'data'),
      ('STR', 'np'),
      ('STR', 'ndarray'),
      ('STR', 'shares_memory'),
      ('STR', 'image'),
      ('STR', 'view'),
      ('STR', '_get_update_slices'),
      ('STR', 'tuple'),
      ('STR', '_align_data_to_view'),
      ('STR', 'raw'),
      ('STR', 'contour'),
      ('STR', 'colormap'),
      ('STR', '_data_to_texture'),
      ('STR', 'array'),
      ('STR', 'dtype'),
      ('STR', 'all'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'region_data'),
      ('STR', 'slice_key'),
      ('STR', 'mask'),
      ('STR', 'new_label'),
      ('STR', 'update_slices'),
      ('STR', 'region_slices'),
      ('STR', 'view_slices'),
      ('STR', 'visible_data'),
      ('STR', 'visible_mask'),
      ('STR', 'new_color'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_refresh_caches_from_region'),
    ('STR', 'Labels._refresh_caches_from_region'),
    ('INT', 1927),
    ('BYTES', ''),
    ('BYTES',
     '8000f03c001014d70f22d10f22d70f29d20f29a854af5ba95bd72d3ed22d3ed80c12f40a000c16906497699169a412a71aa11ad40b2cb412d73141d13141d80c108f4989499074977b917bd71728d11728d7172dd1172df40302320af006000d13e0181cd7182fd1182fb009d3183a880dd80b18d00b20d80c12e02532d10822880d907bf006001823a435a81dd32337d11738880cd8171b9c45a02dd31c30d11731880ce02529d7253dd1253dd80c18982cf30302260ad10822880c906cf40c000d17907497799179a422a72aa12ad40c2ddc1012d71020d11020a014a719a119a844af4ba94bd72c3dd12c3dd72c41d12c41d41042e0384488448f4b894bd70c1dd10c1dd70c21d10c21a425a80bd32234d10c35f00a000c108f3c893c9821d20b1bd80c12f006001519974d914dd71432d11432dc0c0e8f4889489069905ba80cd7283ad1283ad40c3bf30302150ae00a0bf10502150d8809f00a000c18d70b1bd10b1bd40b1dd8394288448f4b894bd70c1dd10c1dd70c22d10c22a435a81bd32335d20c36f00600111af003000d118f4b894bd70c1dd10c1dd70c22d10c22a435a81bd32335d10c36b07cd20c44'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 2),
    ('INT', 0),
    ('INT', 0),
    ('INT', 9),
    ('INT', 6),
    ('INT', 16777219),
    ('BYTES',
     '97007c006a000000000000000000000000000000000000006a020000000000000000000000000000000000007d027c006a05000000000000000000000000000000000000ab000000000000007d03740700000000000000006401ab0100000000000067017c006a080000000000000000000000000000000000007a0500007d04740700000000000000006401ab010000000000006701740b00000000000000007c02ab010000000000007a0500007d05740d00000000000000007c006a08000000000000000000000000000000000000ab0100000000000044005d6400007d067c017c06190000007d077c067c02760072157c077c057c026a0f0000000000000000000000000000000000007c06ab010000000000003c0000008c217c037c06190000007d087c076a100000000000000000000000000000000000007c08630278026b1a000072107c076a120000000000000000000000000000000000006b020000730501007901010001007901741500000000000000007c087c076a100000000000000000000000000000000000007a0a0000ab010000000000007c047c063c0000008c6604007c047c0566025300'),
    ('TUPLE',
     (('STR',
       'Calculate slices for extracting region data and updating the view.\n'
       '\n'
       '        Returns\n'
       '        -------\n'
       '        tuple[list[slice | int], list[slice]] | None\n'
       '            A tuple (region_slices, view_slices). region_slices extracts\n'
       '            the currently visible part of the painted region (integer\n'
       '            indices collapse the non-displayed dims); view_slices addresses\n'
       '            that part within the displayed slice caches.\n'
       '            Returns None if the painted region is not currently visible.\n'
       '        '),
      ('NONE',))),
    ('TUPLE',
     (('STR', '_slice_input'),
      ('STR', 'displayed'),
      ('STR', '_get_pt_not_disp'),
      ('STR', 'slice'),
      ('STR', 'ndim'),
      ('STR', 'len'),
      ('STR', 'range'),
      ('STR', 'index'),
      ('STR', 'start'),
      ('STR', 'stop'),
      ('STR', 'int'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'slice_key'),
      ('STR', 'displayed_dims'),
      ('STR', 'pt_not_disp'),
      ('STR', 'region_slices'),
      ('STR', 'view_slices'),
      ('STR', 'd'),
      ('STR', 'axis_slice'),
      ('STR', 'current_pos'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_get_update_slices'),
    ('STR', 'Labels._get_update_slices'),
    ('INT', 2008),
    ('BYTES', ''),
    ('BYTES',
     '8000f01c001a1ed7192ad1192ad71934d11934880ed8161ad7162bd1162bd3162d880bf40c002d32b024ab4ba83db834bf39b939d12b44880ddc2429a824a34ba03db433b07ed33346d12346880be41116907497799179d611218841d81922a031991c884ad80f10904ed10f22d83741900b984ed71c30d11c30b011d31c33d21034f006001f2aa821996e900bd81822d71828d11828a84bd41849b82abf2fb92fd21849d91b1ff00300194a01d91b1fdc2326a07bb05ad73545d13545d12745d32346900d9861d21020f013001222f01600101d986bd00f29d00829'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 3),
    ('INT', 0),
    ('INT', 0),
    ('INT', 7),
    ('INT', 6),
    ('INT', 16777219),
    ('BYTES',
     '97007c006a000000000000000000000000000000000000006a020000000000000000000000000000000000007d03740500000000000000007c03ab010000000000007d04740700000000000000007c03ab010000000000007c046b370000724a7c0344008f056302670063025d1300007d057c046a090000000000000000000000000000000000007c05ab0100000000000091028c1504007d067d05740b00000000000000006a0c0000000000000000000000000000000000007c017c06ab020000000000007d01740b00000000000000006a0c0000000000000000000000000000000000007c027c06ab020000000000007d027c017c02660253006302010063027d057700'),
    ('TUPLE',
     (('STR',
       'Transpose data from ascending dimension order to the displayed order.\n'
       '\n'
       '        ``displayed`` may not be sorted (e.g. a transposed view), so the\n'
       '        extracted region (whose axes are in ascending dimension order) is\n'
       '        permuted to match the order the display caches expect.\n'
       '        '),)),
    ('TUPLE',
     (('STR', '_slice_input'),
      ('STR', 'displayed'),
      ('STR', 'sorted'),
      ('STR', 'list'),
      ('STR', 'index'),
      ('STR', 'np'),
      ('STR', 'transpose'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'visible_data'),
      ('STR', 'visible_mask'),
      ('STR', 'displayed_dims'),
      ('STR', 'sorted_dims'),
      ('STR', 'd'),
      ('STR', 'perm'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_align_data_to_view'),
    ('STR', 'Labels._align_data_to_view'),
    ('INT', 2045),
    ('BYTES', 'b418413e04'),
    ('BYTES',
     '8000f016001a1ed7192ad1192ad71934d11934880edc161c985ed3162c880bdc0b0f900ed30b1fa03bd20b2ed93240d31341b12ea851904bd71425d11425a061d51428b02e8844d01341dc1b1d9f3c993ca80cb064d31b3b884cdc1b1d9f3c993ca80cb064d31b3b884cd80f1b985cd00f29d00829f9f20700144201'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 1),
    ('INT', 0),
    ('INT', 0),
    ('INT', 4),
    ('INT', 6),
    ('INT', 16777219),
    ('BYTES',
     '97007c006a000000000000000000000000000000000000006a020000000000000000000000000000000000007d01740500000000000000006a060000000000000000000000000000000000007c006a090000000000000000000000000000000000007c016a0a0000000000000000000000000000000000006a0c000000000000000000000000000000000000ab01000000000000ab010000000000006a0f00000000000000000000000000000000000074100000000000000000ab010000000000007d027c016a1200000000000000000000000000000000000044008f036302690063025d0800007d037c037c027c031900000093028c0a040063027d0353006302010063027d037700'),
    ('TUPLE', (('STR', '\n        Get indices of current visible slice.\n        '),)),
    ('TUPLE',
     (('STR', '_slice'),
      ('STR', 'slice_input'),
      ('STR', 'np'),
      ('STR', 'round'),
      ('STR', 'world_to_data'),
      ('STR', 'world_slice'),
      ('STR', 'point'),
      ('STR', 'astype'),
      ('STR', 'int'),
      ('STR', 'not_displayed'))),
    ('TUPLE', (('STR', 'self'), ('STR', 'slice_input'), ('STR', 'point'), ('STR', 'dim'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_get_pt_not_disp'),
    ('STR', 'Labels._get_pt_not_disp'),
    ('INT', 2076),
    ('BYTES', 'c1300d420004'),
    ('BYTES',
     '8000f00800171b976b916bd7162dd1162d880bdc101297089108d80c10d70c1ed10c1e987bd71f36d11f36d71f3cd11f3cd30c3df30302110ae70a10892694138b2bf00500090ef006002c37d72b44d22b44d30f45d12b44a043900390559833915a910fd02b44d10f45d00845f9d20f45'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 2),
    ('INT', 0),
    ('INT', 0),
    ('INT', 3),
    ('INT', 6),
    ('INT', 16777219),
    ('BYTES',
     '97007c006a0000000000000000000000000000000000000064016b02000072027c015300740300000000000000006401740500000000000000007c006a00000000000000000000000000000000000000ab01000000000000ab020000000000007d02740700000000000000007c017c006a080000000000000000000000000000000000006a0a0000000000000000000000000000000000007c02ab030000000000005300'),
    ('TUPLE',
     (('STR', 'Expand dirty slices so contour refresh includes the full contour halo.'), ('INT', 1))),
    ('TUPLE',
     (('STR', 'contour'),
      ('STR', 'max'),
      ('STR', 'int'),
      ('STR', 'expand_slice'),
      ('STR', 'data'),
      ('STR', 'shape'))),
    ('TUPLE', (('STR', 'self'), ('STR', 'updated_slice'), ('STR', 'contour_offset'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_expand_updated_slice_for_contour'),
    ('STR', 'Labels._expand_updated_slice_for_contour'),
    ('INT', 1742),
    ('BYTES', ''),
    ('BYTES',
     '8000f008000c108f3c893c9821d20b1bd81320d00c20e4191c9851a403a044a74ca14cd32031d31932880edc0f1b984da834af39a939af3fa93fb84ed30f4bd0084b'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 2),
    ('INT', 0),
    ('INT', 0),
    ('INT', 2),
    ('INT', 8),
    ('INT', 16777219),
    ('BYTES',
     '97007c006a010000000000000000000000000000000000007c01ab010000000000007d017c006a0200000000000000000000000000000000000080087c017c005f01000000000000000079017405000000000000000064028400740700000000000000007c006a020000000000000000000000000000000000007c016403ac04ab030000000000004400ab00000000000000ab010000000000007c005f0100000000000000007901'),
    ('TUPLE',
     (('STR', 'Merge a newly dirtied region into the pending partial refresh.'),
      ('NONE',),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 3),
        ('INT', 8),
        ('INT', 16777267),
        ('BYTES',
         '4b00010097007c005d4f00005c0200007d017d0274010000000000000000740300000000000000007c016a040000000000000000000000000000000000007c026a04000000000000000000000000000000000000ab02000000000000740700000000000000007c016a080000000000000000000000000000000000007c026a08000000000000000000000000000000000000ab02000000000000ab020000000000009601970101008c5104007900ad037701'),
        ('TUPLE', (('NONE',),)),
        ('TUPLE', (('STR', 'slice'), ('STR', 'min'), ('STR', 'start'), ('STR', 'max'), ('STR', 'stop'))),
        ('TUPLE', (('STR', '.0'), ('STR', 's1'), ('STR', 's2'))),
        ('TUPLE', ()),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'Labels._accumulate_updated_slice.<locals>.<genexpr>'),
        ('INT', 1762),
        ('BYTES', '824115411701'),
        ('BYTES',
         'e800f88000f00003240ae11a4e910690029042f403000d129423906297689168a002a708a108d31229ac33a872af77a977b802bf07b907d32b40d70c41d91a4ef9'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))),
      ('BOOL', True),
      ('TUPLE', (('STR', 'strict'),)))),
    ('TUPLE',
     (('STR', '_expand_updated_slice_for_contour'),
      ('STR', '_updated_slice'),
      ('STR', 'tuple'),
      ('STR', 'zip'))),
    ('TUPLE', (('STR', 'self'), ('STR', 'updated_slice'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_accumulate_updated_slice'),
    ('STR', 'Labels._accumulate_updated_slice'),
    ('INT', 1752),
    ('BYTES', ''),
    ('BYTES',
     '8000f00800191dd7183ed1183eb87dd3184d880de00b0fd70b1ed10b1ed00b26d8222f8844d40c1fd80c12e41e23f10003240ae41a1d9864d71e31d11e31b03dc814d51a4ef30503240af300031f0a8804d5081b'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 1),
    ('INT', 0),
    ('INT', 0),
    ('INT', 7),
    ('INT', 4),
    ('INT', 16777219),
    ('BYTES',
     '8700970089006a00000000000000000000000000000000000000811689006a020000000000000000000000000000000000006a040000000000000000000000000000000000007301790189006a060000000000000000000000000000000000006a080000000000000000000000000000000000007d0189006a0a0000000000000000000000000000000000006a0c0000000000000000000000000000000000006a0e0000000000000000000000000000000000007d027411000000000000000088006601640284087c014400ab00000000000000ab010000000000007d037c0344008f046302670063025d0e00007d047c046a1200000000000000000000000000000000000091028c1004007d057d0489006a1400000000000000000000000000000000000064036b440000721489006a170000000000000000000000000000000000007c027c03ac04ab020000000000007d066e2389006a0a0000000000000000000000000000000000006a0c0000000000000000000000000000000000006a180000000000000000000000000000000000007c03190000007d067c0689006a0a0000000000000000000000000000000000006a0c0000000000000000000000000000000000006a180000000000000000000000000000000000007c033c00000089006a1a0000000000000000000000000000000000006a1d0000000000000000000000000000000000007c067c05ac05ab020000000000000100640189005f00000000000000000079016302010063027d047700'),
    ('TUPLE',
     (('STR', 'Prepares and displays only an updated part of the labels.'),
      ('NONE',),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 3),
        ('INT', 16777267),
        ('BYTES',
         '95014b00010097007c005d1300007d0189026a000000000000000000000000000000000000007c01190000009601970101008c1504007900ad037701'),
        ('TUPLE', (('NONE',),)),
        ('TUPLE', (('STR', '_updated_slice'),)),
        ('TUPLE', (('STR', '.0'), ('STR', 'index'))),
        ('TUPLE', (('STR', 'self'),)),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'Labels._partial_labels_refresh.<locals>.<genexpr>'),
        ('INT', 941),
        ('BYTES', '83191c01'),
        ('BYTES', 'f8e800f88000f000021e0ad93442a8358844d70c1fd10c1fa005d50c26b14ef9'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))),
      ('INT', 0),
      ('TUPLE', (('STR', 'data_slice'),)),
      ('TUPLE', (('STR', 'data'), ('STR', 'offset'))))),
    ('TUPLE',
     (('STR', '_updated_slice'),
      ('STR', '_slicing_state'),
      ('STR', 'loaded'),
      ('STR', '_slice_input'),
      ('STR', 'displayed'),
      ('STR', '_slice'),
      ('STR', 'image'),
      ('STR', 'raw'),
      ('STR', 'tuple'),
      ('STR', 'start'),
      ('STR', 'contour'),
      ('STR', '_raw_to_displayed'),
      ('STR', 'view'),
      ('STR', 'events'),
      ('STR', 'labels_update'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'dims_displayed'),
      ('STR', 'raw_displayed'),
      ('STR', 'updated_slice'),
      ('STR', 'axis_slice'),
      ('STR', 'offset'),
      ('STR', 'colors_sliced'))),
    ('TUPLE', ()),
    ('TUPLE', (('STR', 'self'),)),
    ('STR', '_partial_labels_refresh'),
    ('STR', 'Labels._partial_labels_refresh'),
    ('INT', 931),
    ('BYTES', 'c13313441604'),
    ('BYTES',
     'f88000f006000c10d70b1ed10b1ed00b26a864d72e41d12e41d72e48d22e48d80c12e0191dd7192ad1192ad71934d11934880ed8181c9f0b990bd71829d11829d7182dd1182d880df40600191ef300021e0ad93442f303021e0af30002190a880df10800364301d31143b15da07a902ad71222d31222b05d8806d01143e00b0f8f3c893c9821d20b1bd81c20d71c32d11c32d8101da82df003001d33f300021d0e894df008001d219f4b994bd71c2dd11c2dd71c32d11c32b03dd11c41884df00e00313e88048f0b890bd70819d10819d7081ed1081e987dd1082de0080c8f0b890bd70821d10821a07db856d00821d40844d81e228804d5081bf9f22300124401'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 4),
    ('INT', 0),
    ('INT', 0),
    ('INT', 9),
    ('INT', 7),
    ('INT', 16777219),
    ('BYTES',
     '870997007c006a000000000000000000000000000000000000007c01190000007c026b3700008a097403000000000000000088096601640184087c014400ab00000000000000ab010000000000007d01740500000000000000007c0274060000000000000000ab020000000000007240740900000000000000006a0a0000000000000000000000000000000000007c027c006a0c0000000000000000000000000000000000006a0e0000000000000000000000000000000000006a100000000000000000000000000000000000006a12000000000000000000000000000000000000ac02ab020000000000007d026e397c006a0c0000000000000000000000000000000000006a0e0000000000000000000000000000000000006a100000000000000000000000000000000000006a120000000000000000000000000000000000006a150000000000000000000000000000000000007c02ab010000000000007d02740500000000000000007c02740800000000000000006a16000000000000000000000000000000000000ab0200000000000072057c028909190000007d027c0172127c016403190000006a1800000000000000000000000000000000000064036b280000720179047c006a1b0000000000000000000000000000000000007c01740900000000000000006a1c0000000000000000000000000000000000007c006a000000000000000000000000000000000000007c01190000006405ac06ab020000000000007c026603ab0100000000000001007c027c006a000000000000000000000000000000000000007c013c0000007c006a1f000000000000000000000000000000000000ab000000000000007d04742100000000000000007c017c047c006a0c0000000000000000000000000000000000006a220000000000000000000000000000000000006a24000000000000000000000000000000000000ab030000000000007d05740500000000000000007c02740800000000000000006a16000000000000000000000000000000000000ab0200000000000072107c02742700000000000000007c017c04ab02000000000000190000007d066e027c027d06740500000000000000007c006a00000000000000000000000000000000000000740800000000000000006a16000000000000000000000000000000000000ab02000000000000735274050000000000000000742900000000000000007c006a0000000000000000000000000000000000000064076404ab03000000000000740800000000000000006a16000000000000000000000000000000000000ab0200000000000073237c067c006a0c0000000000000000000000000000000000006a0e0000000000000000000000000000000000006a100000000000000000000000000000000000007c053c000000740500000000000000007c006a00000000000000000000000000000000000000740800000000000000006a16000000000000000000000000000000000000ab0200000000000073307c0144008f076302670063025d2500007d07740900000000000000006a1c0000000000000000000000000000000000007c07ab010000000000006a2b000000000000000000000000000000000000ab0000000000000091028c2704007d017d0774030000000000000000640884007c014400ab00000000000000ab010000000000007d087c006a2c00000000000000000000000000000000000064036b280000723c7c006a2e0000000000000000000000000000000000006a310000000000000000000000000000000000007c06ab010000000000007c006a0c0000000000000000000000000000000000006a0e0000000000000000000000000000000000006a320000000000000000000000000000000000007c053c0000007c006a350000000000000000000000000000000000007c08ab0100000000000001007c036405750072117c006a37000000000000000000000000000000000000ab000000000000000100790479046302010063027d077700'),
    ('TUPLE',
     (('STR',
       'Set `indices` in `data` to `value`, while writing to edit history.\n'
       '\n'
       '        Parameters\n'
       '        ----------\n'
       '        indices : tuple of arrays of int\n'
       '            Indices in data to overwrite. Must be a tuple of arrays of length\n'
       '            equal to the number of data dimensions. (Fancy indexing in [2]_).\n'
       '        value : int or array of int\n'
       '            New label value(s). If more than one value, must match or\n'
       '            broadcast with the given indices.\n'
       '        refresh : bool, default True\n'
       '            whether to refresh the view, by default True\n'
       '\n'
       '        References\n'
       '        ----------\n'
       '        .. [2] https://numpy.org/doc/stable/user/basics.indexing.html\n'
       '        '),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 3),
        ('INT', 16777267),
        ('BYTES', '95014b00010097007c005d0900007d017c018902190000009601970101008c0b04007900ad037701'),
        ('TUPLE', (('NONE',),)),
        ('TUPLE', ()),
        ('TUPLE', (('STR', '.0'), ('STR', 'x'))),
        ('TUPLE', (('STR', 'changed_indices'),)),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'Labels.data_setitem.<locals>.<genexpr>'),
        ('INT', 2105),
        ('BYTES', '830f1201'),
        ('BYTES', 'f8e800f88000d0173cb147a8719801982fd5182ab147f9'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))),
      ('TUPLE', (('STR', 'dtype'),)),
      ('INT', 0),
      ('NONE',),
      ('BOOL', True),
      ('TUPLE', (('STR', 'copy'),)),
      ('STR', 'data'),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 8),
        ('INT', 16777267),
        ('BYTES',
         '4b00010097007c005d4100007d0174010000000000000000740300000000000000007c016a05000000000000000000000000000000000000ab00000000000000ab01000000000000740300000000000000007c016a07000000000000000000000000000000000000ab00000000000000ab0100000000000064007a000000ab020000000000009601970101008c4304007901ad037701'),
        ('TUPLE', (('INT', 1), ('NONE',))),
        ('TUPLE', (('STR', 'slice'), ('STR', 'int'), ('STR', 'min'), ('STR', 'max'))),
        ('TUPLE', (('STR', '.0'), ('STR', 'axis_indices'))),
        ('TUPLE', ()),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'Labels.data_setitem.<locals>.<genexpr>'),
        ('INT', 2155),
        ('BYTES', '824107410901'),
        ('BYTES',
         'e800f88000f000031e0ae12027900cf403000d129423906cd71626d11626d31628d31229ac33a87cd72f3fd12f3fd32f41d32b42c051d12b46d70c47d92027f9'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))))),
    ('TUPLE',
     (('STR', 'data'),
      ('STR', 'tuple'),
      ('STR', 'isinstance'),
      ('STR', 'Sequence'),
      ('STR', 'np'),
      ('STR', 'asarray'),
      ('STR', '_slice'),
      ('STR', 'image'),
      ('STR', 'raw'),
      ('STR', 'dtype'),
      ('STR', 'type'),
      ('STR', 'ndarray'),
      ('STR', 'size'),
      ('STR', '_save_history'),
      ('STR', 'array'),
      ('STR', '_get_pt_not_disp'),
      ('STR', 'index_in_slice'),
      ('STR', 'slice_input'),
      ('STR', 'order'),
      ('STR', 'elements_in_slice'),
      ('STR', 'getattr'),
      ('STR', 'flatten'),
      ('STR', 'contour'),
      ('STR', 'colormap'),
      ('STR', '_data_to_texture'),
      ('STR', 'view'),
      ('STR', '_accumulate_updated_slice'),
      ('STR', '_partial_labels_refresh'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'indices'),
      ('STR', 'value'),
      ('STR', 'refresh'),
      ('STR', 'pt_not_disp'),
      ('STR', 'displayed_indices'),
      ('STR', 'visible_values'),
      ('STR', 'x'),
      ('STR', 'updated_slice'))),
    ('TUPLE', ()),
    ('TUPLE', (('STR', 'changed_indices'),)),
    ('STR', 'data_setitem'),
    ('STR', 'Labels.data_setitem'),
    ('INT', 2086),
    ('BYTES', 'c82f2a4b1f04'),
    ('BYTES',
     'f88000f024001b1f9f299929a047d11a2cb005d11a35880fdc1217d3173cb147d3173cd3123c8807e40b1590659c58d40b26dc1416974a914a9875a844af4ba94bd72c3dd12c3dd72c41d12c41d72c47d12c47d414488945e01418974b914bd71425d11425d71429d11429d7142fd1142fd71434d11434b055d3143b8845f406000c1690659c529f5a995ad40b28d81419982fd1142a8845e10f169827a021992a9f2f992fa851d21a2ed80c12e0080cd7081ad1081ae01017dc10129708910898149f199919a037d1192bb024d41037d81015f007040d0ef40306090af012001e2388048f0989099027d1081ae0161ad7162bd1162bd3162d880bdc1c2ad80c13905ba024a72ba12bd72239d12239d7223fd1223ff303021d0ad00819f406000c1690659c529f5a995ad40b28d81d22d42334b057b86bd3234ad11d4b894ee01d22884ef406000d17907497799179a422a72aa12ad40c2ddc0f199c27a024a729a129a856b054d31a3abc42bf4ab94ad40f47f00e0038460188448f4b894bd70c1dd10c1dd70c21d10c21d02233d10c34f40800101a98249f299929a452a75aa15ad40f30d9363dd3163eb167b011947297789178a001937bd7172ad1172ad5172cb0678847d0163ee4181df100031e0ae12027f305031e0af30003190a880df00a000c108f3c893c9831d20b1cf006001115970d910dd7102ed1102ea87ed3103ef003000d118f4b894bd70c1dd10c1dd70c22d10c22d02334d10c35f00800090dd70826d10826a07dd40835e00b129064893fd80c10d70c28d10c28d50c2af003000c1bf9f21f00173f'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 2),
    ('INT', 0),
    ('INT', 0),
    ('INT', 5),
    ('INT', 6),
    ('INT', 3),
    ('BYTES',
     '97007c016a01000000000000000000000000000000000000ab0000000000000044008f028f036303670063025d0d00005c0200007d027d037c007c02190000007c036b28000091028c0f04007d047d027d03740200000000000000006a040000000000000000000000000000000000006a070000000000000000000000000000000000007c046401ac02ab0200000000000053006302010063037d037d027700'),
    ('TUPLE',
     (('STR',
       'Mask elements from a multi-dimensional index not in a given slice.\n'
       '\n'
       '    Some n-D operations may edit data that is not visible in the current slice.\n'
       '    Given slice position information (as a dictionary mapping axis to index on that\n'
       '    axis), this function returns a boolean mask for the possibly higher-dimensional\n'
       '    multi-index so that elements not currently visible are masked out. The\n'
       '    resulting multi-index can then be subset and used to index into a texture or\n'
       '    other lower-dimensional view.\n'
       '\n'
       '    Parameters\n'
       '    ----------\n'
       '    index : tuple of array of int\n'
       '        A NumPy fancy indexing expression [1]_.\n'
       '    position_in_axes : dict[int, int]\n'
       '        A dictionary mapping sliced (non-displayed) axes to a slice position.\n'
       '\n'
       '    Returns\n'
       '    -------\n'
       '    visible : array of bool\n'
       '        A boolean array indicating which items are visible in the current view.\n'
       '    '),
      ('INT', 0),
      ('TUPLE', (('STR', 'axis'),)))),
    ('TUPLE', (('STR', 'items'), ('STR', 'np'), ('STR', 'logical_and'), ('STR', 'reduce'))),
    ('TUPLE',
     (('STR', 'index'), ('STR', 'position_in_axes'), ('STR', 'ax'), ('STR', 'position'), ('STR', 'queries'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', 'elements_in_slice'),
    ('STR', 'elements_in_slice'),
    ('INT', 5),
    ('BYTES', '9412410a06'),
    ('BYTES',
     '8000f03200334301d73248d13248d4324af403020f06d9324aa12ca022a0688805886289099058d3081dd0324af00300050cf100020f06f406000c0e8f3e893ed70b20d10b20a017a871d00b20d30b31d00431f9f307020f06'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\utils\\_indexing.py')),
   (('INT', 3),
    ('INT', 0),
    ('INT', 0),
    ('INT', 3),
    ('INT', 5),
    ('INT', 3),
    ('BYTES',
     '87008701870397007401000000000000000089008901ab020000000000008a03740300000000000000008800880388016603640184087c024400ab00000000000000ab010000000000005300'),
    ('TUPLE',
     (('STR',
       'Convert a NumPy fancy indexing expression from data to sliced space.\n'
       '\n'
       '    Parameters\n'
       '    ----------\n'
       '    index : tuple of array of int\n'
       '        A NumPy fancy indexing expression [1]_.\n'
       '    position_in_axes : dict[int, int]\n'
       '        A dictionary mapping sliced (non-displayed) axes to a slice position.\n'
       '    indices_order : tuple of int\n'
       '        The order of the indices in data view.\n'
       '\n'
       '    Returns\n'
       '    -------\n'
       '    sliced_index : tuple of array of int\n'
       '        The indexing expression (nD) restricted to the current slice (usually\n'
       '        2D or 3D).\n'
       '\n'
       '    Examples\n'
       '    --------\n'
       '    >>> index = (np.arange(5), np.full(5, 1), np.arange(4, 9))\n'
       '    >>> index_in_slice(index, {0: 3}, (0, 1, 2))\n'
       '    (array([1]), array([7]))\n'
       '    >>> index_in_slice(index, {1: 1, 2: 8}, (0, 1, 2))\n'
       '    (array([4]),)\n'
       '\n'
       '    References\n'
       '    ----------\n'
       '    [1]: https://numpy.org/doc/stable/user/basics.indexing.html#integer-array-indexing\n'
       '    '),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 3),
        ('INT', 51),
        ('BYTES',
         '95034b00010097007c005d1000007d017c0189047601720a89027c01190000008903190000009601970101008c1204007900ad037701'),
        ('TUPLE', (('NONE',),)),
        ('TUPLE', ()),
        ('TUPLE', (('STR', '.0'), ('STR', 'i'))),
        ('TUPLE', (('STR', 'index'), ('STR', 'index_in_slice'), ('STR', 'position_in_axes'))),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'index_in_slice.<locals>.<genexpr>'),
        ('INT', 70),
        ('BYTES', '83161901'),
        ('BYTES', 'f8e800f88000f000041106e1111e8841d80b0cd01424d10b24f00500090e88618908901ed50820d9111ef9'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\utils\\_indexing.py'))))),
    ('TUPLE', (('STR', 'elements_in_slice'), ('STR', 'tuple'))),
    ('TUPLE', (('STR', 'index'), ('STR', 'position_in_axes'), ('STR', 'indices_order'))),
    ('TUPLE', ()),
    ('TUPLE', (('STR', 'index'), ('STR', 'position_in_axes'), ('STR', 'index_in_slice'))),
    ('STR', 'index_in_slice'),
    ('STR', 'index_in_slice'),
    ('INT', 35),
    ('BYTES', ''),
    ('BYTES', 'fa8000f44401001627a075d02e3ed3153f804edc0b10f500041106e1111ef305041106f300040c06f000040506'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\utils\\_indexing.py')))),
 (13,
  ((('INT', 7),
    ('INT', 0),
    ('INT', 0),
    ('INT', 9),
    ('INT', 6),
    ('INT', 16777219),
    ('BYTES',
     '5e0495005b0100000000000000005504340153011a006a085b0300000000000000005500520400000000000000000000000000000000000035010000000000001300350000000000000035010000000000006e0755072800000000000000612f00005b06000000000000000052080000000000000000000000000000000000002200582735020000000000006e025506621600005b06000000000000000052080000000000000000000000000000000000002200586735020000000000006e065506632300005b060000000000000000520a00000000000000000000000000000000000022005500520c00000000000000000000000000000000000055010500000035010000000000006e065500520f0000000000000000000000000000000000005862583135040000000000006e0855086301000067025860520c000000000000000000000000000000000000550127000000550052110000000000000000000000000000000000005861588335040000000000002000550052130000000000000000000000000000000000005501350100000000000020005505280000000000000061110000550052150000000000000000000000000000000000003500000000000000200067026702'),
    ('TUPLE',
     (('STR',
       'Universal painting method using a boolean mask within a bounding box.\n'
       '\n'
       'This method extracts the common pattern shared by brush painting, fill\n'
       'and polygon painting: extract a region, apply a mask, write back, update\n'
       'caches. It manages cache updates for non-shared memory backends, respects\n'
       'the preserve_labels setting (handled by _apply_mask_to_data), and updates\n'
       'undo/redo history.\n'
       '\n'
       'Parameters\n'
       '----------\n'
       'slice_key : tuple of slice\n'
       '    N-dimensional slice key in volume space specifying the bounding box\n'
       '    region; length-1 slices for non-painted dims. Example:\n'
       '    (slice(5, 6), slice(100, 200), slice(150, 250)) for a 3D volume\n'
       '    where dims_to_paint=[1, 2].\n'
       'mask : ndarray\n'
       '    Boolean mask indicating which pixels to paint within the bounding\n'
       '    box, with one axis per painted dimension.\n'
       'new_label : int\n'
       '    Label value to paint.\n'
       'dims_to_paint : list of int\n'
       '    Indices of dimensions being painted (e.g., [1, 2] for YX in a ZYX volume).\n'
       'refresh : bool, optional\n'
       '    Whether to refresh the display after painting. Default is True.\n'
       'region_data : np.ndarray | None, optional\n'
       '    Pre-loaded data for the region defined by slice_key, with one axis\n'
       '    per painted dimension. If None (default), data will be read from\n'
       '    self.data[slice_key]. Providing this avoids redundant reads when\n'
       '    data is already available (e.g. in fill).\n'),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 3),
        ('INT', 16777267),
        ('BYTES',
         '3e0123002000950055001300480e00006f1154023b010000640200004d0a000055017600950520004d1000000b002000670037036601'),
        ('TUPLE', (('NONE',),)),
        ('TUPLE', ()),
        ('TUPLE', (('STR', '.0'), ('STR', 'dim'))),
        ('TUPLE', (('STR', 'dims_to_paint'),)),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'Labels._paint_region_with_mask.<locals>.<genexpr>'),
        ('INT', 1809),
        ('BYTES', '8309190190091901'),
        ('BYTES', 'f8e9008000f000021b0ada1b2b9043b82dd12f478f438943d21b2bf9'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))),
      ('NONE',))),
    ('TUPLE',
     (('STR', 'tuple'),
      ('STR', 'range'),
      ('STR', 'ndim'),
      ('STR', 'np'),
      ('STR', 'expand_dims'),
      ('STR', 'asarray'),
      ('STR', 'data'),
      ('STR', '_apply_mask_to_data'),
      ('STR', '_refresh_caches_from_region'),
      ('STR', '_accumulate_updated_slice'),
      ('STR', '_partial_labels_refresh'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'slice_key'),
      ('STR', 'mask'),
      ('STR', 'new_label'),
      ('STR', 'dims_to_paint'),
      ('STR', 'refresh'),
      ('STR', 'region_data'),
      ('STR', 'extra_axes'),
      ('STR', 'effective_mask'))),
    ('TUPLE', ()),
    ('TUPLE', (('STR', 'dims_to_paint'),)),
    ('STR', '_paint_region_with_mask'),
    ('STR', 'Labels._paint_region_with_mask'),
    ('INT', 1767),
    ('BYTES', ''),
    ('BYTES',
     'f88000f4540100161bf400021b0adc1b20a014a719a119d41b2bf303021b0af30002160a880af606000c16dc1315973e923ea024d313338844d80f1ad10f26dc1e209f6e9a6ea85bd31e45900be00b16d10b1edc1a1c9f2a9a2aa054a759a159a879d12539d31a3a884be0191dd71931d11931d80c179879f303021a0a880ef008000c1ad10b21d80c12f00c00202b8f0989099029d1081cf00800090dd70828d10828d80c17a04ef40302090af00a00090dd70826d10826a079d40831e60b12d80c10d70c28d10c28d50c2af003000c13'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 5),
    ('INT', 0),
    ('INT', 0),
    ('INT', 8),
    ('INT', 8),
    ('INT', 16777219),
    ('BYTES',
     '95005b0000000000000000005202000000000000000000000000000000000000220055025301530239026e055500520400000000000000000000000000000000000028000000000000006135000055005207000000000000000000000000000000000000550335010000000000006e0658163a4800006e075b0000000000000000005208000000000000000000000000000000000000220055073501000000000000280000000000000064010000670358572d0e00006e05585155033a6700002d0e00006e055b000000000000000000520800000000000000000000000000000000000022005505350100000000000028000000000000006401000067035500520b0000000000000000000000000000000000005500520d00000000000000000000000000000000000058415853350400000000000035010000000000002000583155052700000055052400'),
    ('TUPLE',
     (('STR',
       'Paint within a data region using a boolean mask.\n'
       '\n'
       'This method operates on the data array in-place, using boolean mask\n'
       'indexing. It handles preserve_labels logic and optimization to skip\n'
       'unchanged pixels. The return value is the effective mask of pixels that\n'
       'were actually painted.\n'
       '\n'
       'Parameters\n'
       '----------\n'
       'data : np.ndarray\n'
       '    The data to paint (e.g., extracted region/bounding box around polygon).\n'
       '    This array is modified IN PLACE.\n'
       'mask : np.ndarray\n'
       '    Boolean mask indicating which pixels to paint, with shape matching data.\n'
       'new_label : int\n'
       '    Label value to paint.\n'
       'volume_slice : tuple of slice\n'
       '    Slice key in volume space for undo/redo history.\n'
       '\n'
       'Returns\n'
       '-------\n'
       'np.ndarray | None\n'
       '    Boolean mask of the pixels that were painted, or None if the\n'
       '    operation made no changes.\n'
       '\n'
       'Notes\n'
       '-----\n'
       '- Saves to undo/redo history\n'
       "- Does NOT update caches or refresh display (caller's responsibility)\n"),
      ('BOOL', True),
      ('TUPLE', (('STR', 'copy'),)),
      ('NONE',))),
    ('TUPLE',
     (('STR', 'np'),
      ('STR', 'array'),
      ('STR', 'preserve_labels'),
      ('STR', '_get_preserve_labels_source_label'),
      ('STR', 'any'),
      ('STR', '_save_history'),
      ('STR', '_history_atom_for_mask_paint'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'data'),
      ('STR', 'mask'),
      ('STR', 'new_label'),
      ('STR', 'volume_slice'),
      ('STR', 'effective_mask'),
      ('STR', 'source_label'),
      ('STR', 'keep_mask'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_apply_mask_to_data'),
    ('STR', 'Labels._apply_mask_to_data'),
    ('INT', 1845),
    ('BYTES', ''),
    ('BYTES',
     '8000f44a01001a1c9f189a18a024a854d11932880ee00b0fd70b1fd70b1fd81b1fd71b41d11b41c029d31b4c884cd8181cd1182c8849dc1315973692369829d71324d11324d8171bd80c1ad10c27884ee00816a029d11a2bd1082b880edc0f118f768a76906ed70f25d10f25d81317e0080cd7081ad1081ad80c10d70c2dd10c2dd8101ca04ef303020d0ef40304090af00a002029885ed1081ce00f1dd0081d'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 2),
    ('INT', 0),
    ('INT', 0),
    ('INT', 2),
    ('INT', 2),
    ('INT', 16777219),
    ('BYTES',
     '95005810520000000000000000000000000000000000000052020000000000000000000000000000000000003a5800006125000055005204000000000000000000000000000000000000620c00005500520400000000000000000000000000000000000024005500520600000000000000000000000000000000000024005500520000000000000000000000000000000000000052020000000000000000000000000000000000002400'),
    ('TUPLE',
     (('STR',
       'Return the existing label value that preserve_labels allows to change.\n'
       '\n'
       'Painting with a non-background label may only replace background.\n'
       'Painting with the background label may only replace the previously\n'
       'selected label, falling back to the current selected label when no\n'
       'previous label is stored.\n'
       '\n'
       'Parameters\n'
       '----------\n'
       'new_label : int\n'
       '    The label value that will be written.\n'
       '\n'
       'Returns\n'
       '-------\n'
       'int\n'
       '    The only existing label value that may be overwritten.\n'),)),
    ('TUPLE',
     (('STR', 'colormap'),
      ('STR', 'background_value'),
      ('STR', '_prev_selected_label'),
      ('STR', 'selected_label'))),
    ('TUPLE', (('STR', 'self'), ('STR', 'new_label'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_get_preserve_labels_source_label'),
    ('STR', 'Labels._get_preserve_labels_source_label'),
    ('INT', 1341),
    ('BYTES', ''),
    ('BYTES',
     '8000f024000c159f0d990dd71836d11836d30b36d80f13d70f28d10f28d10f34d8171bd71730d11730d01030d81317d71326d11326d00c26d80f138f7d897dd70f2dd10f2dd0082d'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 5),
    ('INT', 0),
    ('INT', 0),
    ('INT', 11),
    ('INT', 7),
    ('INT', 16777219),
    ('BYTES',
     '95005500520000000000000000000000000000000000000052020000000000000000000000000000000000002800000000000000611b000055005204000000000000000000000000000000000000520600000000000000000000000000000000000028000000000000006101000067015b0900000000000000005500520a0000000000000000000000000000000000005b0c0000000000000000520e00000000000000000000000000000000000035020000000000002800000000000000614500005b0c0000000000000000521000000000000000000000000000000000000022005500520a00000000000000000000000000000000000055005204000000000000000000000000000000000000521200000000000000000000000000000000000052140000000000000000000000000000000000003502000000000000280000000000000061010000670155005217000000000000000000000000000000000000550235010000000000006e055505630100006701550575020000706755015b19000000000000000055063501000000000000050000006e0855035b19000000000000000055063501000000000000050000006e095500521b000000000000000000000000000000000000588935020000000000007502000070895b0900000000000000005500520a0000000000000000000000000000000000005b0c0000000000000000520e00000000000000000000000000000000000035020000000000002800000000000000614400005b0c0000000000000000521000000000000000000000000000000000000022005500520a000000000000000000000000000000000000550052040000000000000000000000000000000000005212000000000000000000000000000000000000521c00000000000000000000000000000000000035020000000000002800000000000000642b0000588052040000000000000000000000000000000000005212000000000000000000000000000000000000521c0000000000000000000000000000000000005b19000000000000000055073501000000000000270000005500521e00000000000000000000000000000000000053023a9400006101000067015500522000000000000000000000000000000000000052230000000000000000000000000000000000005b0c00000000000000005224000000000000000000000000000000000000220055042f01550852260000000000000000000000000000000000005303390235010000000000005302050000006e0a5509522900000000000000000000000000000000000035000000000000002800000000000000612c000058a05204000000000000000000000000000000000000521200000000000000000000000000000000000052140000000000000000000000000000000000005b19000000000000000055073501000000000000270000006701550a55005204000000000000000000000000000000000000521200000000000000000000000000000000000052140000000000000000000000000000000000005b19000000000000000055073501000000000000050000005509270000006701'),
    ('TUPLE',
     (('STR',
       'Update raw and view caches with new data from a painted region.\n'
       '\n'
       'Checks if the painted region intersects with the currently displayed slice.\n'
       'If so, extracts the visible sub-region and updates the caches. This method\n'
       'handles painting on non-shared memory backends (zarr, tensorstore, etc.)\n'
       'that require manual cache updates.\n'
       '\n'
       'Parameters\n'
       '----------\n'
       'region_data : np.ndarray\n'
       '    The data of the painted region, with full data dimensionality.\n'
       'slice_key : tuple of slice\n'
       '    The slices used to extract region_data from the full volume;\n'
       '    length-1 slices for non-painted dims.\n'
       'mask : np.ndarray\n'
       '    Boolean mask indicating modified pixels in region_data.\n'
       '    Used to optimize texture updates.\n'
       'new_label : int\n'
       '    The new label value that was painted. Used to optimize texture updates.\n'),
      ('NONE',),
      ('INT', 0),
      ('TUPLE', (('STR', 'dtype'),)))),
    ('TUPLE',
     (('STR', '_slicing_state'),
      ('STR', 'loaded'),
      ('STR', '_slice'),
      ('STR', 'empty'),
      ('STR', 'isinstance'),
      ('STR', 'data'),
      ('STR', 'np'),
      ('STR', 'ndarray'),
      ('STR', 'shares_memory'),
      ('STR', 'image'),
      ('STR', 'view'),
      ('STR', '_get_update_slices'),
      ('STR', 'tuple'),
      ('STR', '_align_data_to_view'),
      ('STR', 'raw'),
      ('STR', 'contour'),
      ('STR', 'colormap'),
      ('STR', '_data_to_texture'),
      ('STR', 'array'),
      ('STR', 'dtype'),
      ('STR', 'all'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'region_data'),
      ('STR', 'slice_key'),
      ('STR', 'mask'),
      ('STR', 'new_label'),
      ('STR', 'update_slices'),
      ('STR', 'region_slices'),
      ('STR', 'view_slices'),
      ('STR', 'visible_data'),
      ('STR', 'visible_mask'),
      ('STR', 'new_color'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_refresh_caches_from_region'),
    ('STR', 'Labels._refresh_caches_from_region'),
    ('INT', 1927),
    ('BYTES', ''),
    ('BYTES',
     '8000f03c001014d70f22d10f22d70f29d70f29a854af5ba95bd72d3ed72d3ed80c12f40a000c16906497699169a412a71aa11ad70b2cd10b2cb412d73141d23141d80c108f4989499074977b917bd71728d11728d7172dd1172df70302320af10002320af006000d13e0181cd7182fd1182fb009d3183a880dd80b18d10b20d80c12e02532d10822880df006001823a435a81dd32337d11738880cd8171b9c45a02dd31c30d11731880ce02529d7253dd1253dd80c18f30302260ad10822880cf40c000d17907497799179a422a72aa12ad70c2dd10c2ddc1012d71020d21020a014a719a119a844af4ba94bd72c3dd12c3dd72c41d12c41d71042d11042e038448f4b894bd70c1dd10c1dd70c21d10c21a425a80bd32234d10c35f00a000c108f3c893c9821d30b1bd80c12f006001519974d914dd71432d11432dc0c0e8f488a489069905ba80cd7283ad1283ad10c3bf30302150ae00a0bf10502150d8809f00a000c18d70b1bd10b1bd70b1dd10b1dd839428f4b894bd70c1dd10c1dd70c22d10c22a435a81bd32335d20c36f00600111af003000d118f4b894bd70c1dd10c1dd70c22d10c22a435a81bd32335d10c36b07cd20c44'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 2),
    ('INT', 0),
    ('INT', 0),
    ('INT', 9),
    ('INT', 6),
    ('INT', 16777219),
    ('BYTES',
     '95005500520000000000000000000000000000000000000052020000000000000000000000000000000000006e025500520500000000000000000000000000000000000035000000000000006e035b070000000000000000530135010000000000002f01550052080000000000000000000000000000000000002d0500006e045b070000000000000000530135010000000000002f015b0b0000000000000000550235010000000000002d0500006e055b0d00000000000000005500520800000000000000000000000000000000000035010000000000001300486500006e065816050000006e0758623b0000006115000058755502520f00000000000000000000000000000000000055063501000000000000270000004d2100005836050000006e0855075210000000000000000000000000000000000000550873023d023a3a000061120000550752120000000000000000000000000000000000003a120000640600001e00200067012000200067015b150000000000000000588752100000000000000000000000000000000000002d0a000035010000000000005846270000004d6700000b002000584534022400'),
    ('TUPLE',
     (('STR',
       'Calculate slices for extracting region data and updating the view.\n'
       '\n'
       'Returns\n'
       '-------\n'
       'tuple[list[slice | int], list[slice]] | None\n'
       '    A tuple (region_slices, view_slices). region_slices extracts\n'
       '    the currently visible part of the painted region (integer\n'
       '    indices collapse the non-displayed dims); view_slices addresses\n'
       '    that part within the displayed slice caches.\n'
       '    Returns None if the painted region is not currently visible.\n'),
      ('NONE',))),
    ('TUPLE',
     (('STR', '_slice_input'),
      ('STR', 'displayed'),
      ('STR', '_get_pt_not_disp'),
      ('STR', 'slice'),
      ('STR', 'ndim'),
      ('STR', 'len'),
      ('STR', 'range'),
      ('STR', 'index'),
      ('STR', 'start'),
      ('STR', 'stop'),
      ('STR', 'int'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'slice_key'),
      ('STR', 'displayed_dims'),
      ('STR', 'pt_not_disp'),
      ('STR', 'region_slices'),
      ('STR', 'view_slices'),
      ('STR', 'd'),
      ('STR', 'axis_slice'),
      ('STR', 'current_pos'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_get_update_slices'),
    ('STR', 'Labels._get_update_slices'),
    ('INT', 2008),
    ('BYTES', ''),
    ('BYTES',
     '8000f01c001a1ed7192ad1192ad71934d11934880ed8161ad7162bd1162bd3162d880bf40c002d32b024ab4ba83db834bf39b939d12b44880ddc2429a824a34ba03db433b07ed33346d12346880be41116907497799179d611218841d81922991c884ad80f10d30f22d83741984ed71c30d11c30b011d31c33d31034f006001f2a996e900bd81822d71828d11828a84bd51849b82abf2fb92fd41849d91b1ff00300194a01d91b1fdc2326a07bd73545d13545d12745d32346900dd31020f113001222f01600101dd00f29d00829'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 3),
    ('INT', 0),
    ('INT', 0),
    ('INT', 7),
    ('INT', 6),
    ('INT', 16777219),
    ('BYTES',
     '95005500520000000000000000000000000000000000000052020000000000000000000000000000000000006e035b050000000000000000550335010000000000006e045b0700000000000000005503350100000000000055043a770000614c000055031300560573022f0073021300481300006f5452090000000000000000000000000000000000005505350100000000000050024d1500000b0020006e066e055b0a0000000000000000520c0000000000000000000000000000000000002200581635020000000000006e015b0a0000000000000000520c0000000000000000000000000000000000002200582635020000000000006e025812340224007302200073026e056600'),
    ('TUPLE',
     (('STR',
       'Transpose data from ascending dimension order to the displayed order.\n'
       '\n'
       '``displayed`` may not be sorted (e.g. a transposed view), so the\n'
       'extracted region (whose axes are in ascending dimension order) is\n'
       'permuted to match the order the display caches expect.\n'),)),
    ('TUPLE',
     (('STR', '_slice_input'),
      ('STR', 'displayed'),
      ('STR', 'sorted'),
      ('STR', 'list'),
      ('STR', 'index'),
      ('STR', 'np'),
      ('STR', 'transpose'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'visible_data'),
      ('STR', 'visible_mask'),
      ('STR', 'displayed_dims'),
      ('STR', 'sorted_dims'),
      ('STR', 'd'),
      ('STR', 'perm'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_align_data_to_view'),
    ('STR', 'Labels._align_data_to_view'),
    ('INT', 2045),
    ('BYTES', 'b51a420004'),
    ('BYTES',
     '8000f016001a1ed7192ad1192ad71934d11934880edc161c985ed3162c880bdc0b0f900ed30b1fa03bd30b2ed93240d31341b22ea851d71425d11425a061d61428b12e8844d01341dc1b1d9f3c9a3ca80cd31b3b884cdc1b1d9f3c9a3ca80cd31b3b884cd80f1bd00f29d00829f9f20700144201'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 1),
    ('INT', 0),
    ('INT', 0),
    ('INT', 4),
    ('INT', 6),
    ('INT', 16777219),
    ('BYTES',
     '95005500520000000000000000000000000000000000000052020000000000000000000000000000000000006e015b04000000000000000052060000000000000000000000000000000000002200550052090000000000000000000000000000000000005501520a000000000000000000000000000000000000520c00000000000000000000000000000000000035010000000000003501000000000000520f0000000000000000000000000000000000005b10000000000000000035010000000000006e0255015212000000000000000000000000000000000000130056037302300073021300480700006f335823050000005f024d0900000b00200073026e0324007302200073026e036600'),
    ('TUPLE', (('STR', '\nGet indices of current visible slice.\n'),)),
    ('TUPLE',
     (('STR', '_slice'),
      ('STR', 'slice_input'),
      ('STR', 'np'),
      ('STR', 'round'),
      ('STR', 'world_to_data'),
      ('STR', 'world_slice'),
      ('STR', 'point'),
      ('STR', 'astype'),
      ('STR', 'int'),
      ('STR', 'not_displayed'))),
    ('TUPLE', (('STR', 'self'), ('STR', 'slice_input'), ('STR', 'point'), ('STR', 'dim'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_get_pt_not_disp'),
    ('STR', 'Labels._get_pt_not_disp'),
    ('INT', 2076),
    ('BYTES', 'c1310e420204'),
    ('BYTES',
     '8000f00800171b976b916bd7162dd1162d880bdc101297089208d80c10d70c1ed10c1e987bd71f36d11f36d71f3cd11f3cd30c3df30302110ae70a10892694138b2bf00500090ef006002c37d72b44d22b44d30f45d22b44a0439055915a920fd12b44d10f45d00845f9d20f45'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 2),
    ('INT', 0),
    ('INT', 0),
    ('INT', 3),
    ('INT', 6),
    ('INT', 16777219),
    ('BYTES',
     '95005500520000000000000000000000000000000000000053013a12000061020000550124005b03000000000000000053015b05000000000000000055005200000000000000000000000000000000000000350100000000000035020000000000006e025b07000000000000000058105208000000000000000000000000000000000000520a000000000000000000000000000000000000550235030000000000002400'),
    ('TUPLE',
     (('STR', 'Expand dirty slices so contour refresh includes the full contour halo.'), ('INT', 1))),
    ('TUPLE',
     (('STR', 'contour'),
      ('STR', 'max'),
      ('STR', 'int'),
      ('STR', 'expand_slice'),
      ('STR', 'data'),
      ('STR', 'shape'))),
    ('TUPLE', (('STR', 'self'), ('STR', 'updated_slice'), ('STR', 'contour_offset'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_expand_updated_slice_for_contour'),
    ('STR', 'Labels._expand_updated_slice_for_contour'),
    ('INT', 1742),
    ('BYTES', ''),
    ('BYTES',
     '8000f008000c108f3c893c9821d30b1bd81320d00c20e4191c9851a403a044a74ca14cd32031d31932880edc0f1b984daf39a939af3fa93fb84ed30f4bd0084b'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 2),
    ('INT', 0),
    ('INT', 0),
    ('INT', 2),
    ('INT', 9),
    ('INT', 16777219),
    ('BYTES',
     '950055005201000000000000000000000000000000000000550135010000000000006e01550052020000000000000000000000000000000000006307000058106c01000000000000000067015b05000000000000000053021a005b07000000000000000055005202000000000000000000000000000000000000550153035304390313003500000000000000350100000000000055006c0100000000000000006701'),
    ('TUPLE',
     (('STR', 'Merge a newly dirtied region into the pending partial refresh.'),
      ('NONE',),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 3),
        ('INT', 8),
        ('INT', 16777267),
        ('BYTES',
         '23002000950055001300484f00007502000070125b0100000000000000005b030000000000000000550152040000000000000000000000000000000000005502520400000000000000000000000000000000000035020000000000005b0700000000000000005501520800000000000000000000000000000000000055025208000000000000000000000000000000000000350200000000000035020000000000007600950520004d5100000b002000670037036601'),
        ('TUPLE', (('NONE',),)),
        ('TUPLE', (('STR', 'slice'), ('STR', 'min'), ('STR', 'start'), ('STR', 'max'), ('STR', 'stop'))),
        ('TUPLE', (('STR', '.0'), ('STR', 's1'), ('STR', 's2'))),
        ('TUPLE', ()),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'Labels._accumulate_updated_slice.<locals>.<genexpr>'),
        ('INT', 1762),
        ('BYTES', '824117411901'),
        ('BYTES',
         'e9008000f00003240ae21a4e91069002f403000d129423906297689168a002a708a108d31229ac33a872af77a977b802bf07b907d32b40d70c41d00c41da1a4ef9'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))),
      ('BOOL', True),
      ('TUPLE', (('STR', 'strict'),)))),
    ('TUPLE',
     (('STR', '_expand_updated_slice_for_contour'),
      ('STR', '_updated_slice'),
      ('STR', 'tuple'),
      ('STR', 'zip'))),
    ('TUPLE', (('STR', 'self'), ('STR', 'updated_slice'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', '_accumulate_updated_slice'),
    ('STR', 'Labels._accumulate_updated_slice'),
    ('INT', 1752),
    ('BYTES', ''),
    ('BYTES',
     '8000f00800191dd7183ed1183eb87dd3184d880de00b0fd70b1ed10b1ed10b26d8222fd40c1fd80c12e41e23f10003240ae41a1d9864d71e31d11e31b03dc814d21a4ef30503240af300031f0a8804d5081b'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 1),
    ('INT', 0),
    ('INT', 0),
    ('INT', 7),
    ('INT', 5),
    ('INT', 16777219),
    ('BYTES',
     '5e00950054005200000000000000000000000000000000000000621b000054005202000000000000000000000000000000000000520400000000000000000000000000000000000028000000000000006401000067015400520600000000000000000000000000000000000052080000000000000000000000000000000000006e015400520a000000000000000000000000000000000000520c000000000000000000000000000000000000520e0000000000000000000000000000000000006e025b1100000000000000005500340153021a006a0855011300350000000000000035010000000000006e0355031300560473022f0073021300480e00006f44521200000000000000000000000000000000000050024d1000000b0020006e056e045400521400000000000000000000000000000000000053033a94000061100000540052170000000000000000000000000000000000005823530439026e064f235400520a000000000000000000000000000000000000520c00000000000000000000000000000000000052180000000000000000000000000000000000005503050000006e0655065400520a000000000000000000000000000000000000520c00000000000000000000000000000000000052180000000000000000000000000000000000005503270000005400521a000000000000000000000000000000000000521d0000000000000000000000000000000000005865530539022000530154006c00000000000000000067017302200073026e046600'),
    ('TUPLE',
     (('STR', 'Prepares and displays only an updated part of the labels.'),
      ('NONE',),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 3),
        ('INT', 16777267),
        ('BYTES',
         '3e0123002000950055001300481400006e01540252000000000000000000000000000000000000005501050000007600950520004d1600000b002000670037036601'),
        ('TUPLE', (('NONE',),)),
        ('TUPLE', (('STR', '_updated_slice'),)),
        ('TUPLE', (('STR', '.0'), ('STR', 'index'))),
        ('TUPLE', (('STR', 'self'),)),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'Labels._partial_labels_refresh.<locals>.<genexpr>'),
        ('INT', 941),
        ('BYTES', '831c1f01'),
        ('BYTES', 'f8e9008000f000021e0ada3442a8358844d70c1fd10c1fa005d60c26b24ef9'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))),
      ('INT', 0),
      ('TUPLE', (('STR', 'data_slice'),)),
      ('TUPLE', (('STR', 'data'), ('STR', 'offset'))))),
    ('TUPLE',
     (('STR', '_updated_slice'),
      ('STR', '_slicing_state'),
      ('STR', 'loaded'),
      ('STR', '_slice_input'),
      ('STR', 'displayed'),
      ('STR', '_slice'),
      ('STR', 'image'),
      ('STR', 'raw'),
      ('STR', 'tuple'),
      ('STR', 'start'),
      ('STR', 'contour'),
      ('STR', '_raw_to_displayed'),
      ('STR', 'view'),
      ('STR', 'events'),
      ('STR', 'labels_update'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'dims_displayed'),
      ('STR', 'raw_displayed'),
      ('STR', 'updated_slice'),
      ('STR', 'axis_slice'),
      ('STR', 'offset'),
      ('STR', 'colors_sliced'))),
    ('TUPLE', ()),
    ('TUPLE', (('STR', 'self'),)),
    ('STR', '_partial_labels_refresh'),
    ('STR', 'Labels._partial_labels_refresh'),
    ('INT', 931),
    ('BYTES', 'c13a15441804'),
    ('BYTES',
     'f88000f006000c10d70b1ed10b1ed10b26a864d72e41d12e41d72e48d72e48d80c12e0191dd7192ad1192ad71934d11934880ed8181c9f0b990bd71829d11829d7182dd1182d880df40600191ef400021e0ad93442f303021e0af30002190a880df10800364301d31143b25da07ad71222d41222b15d8806d01143e00b0f8f3c893c9821d30b1bd81c20d71c32d11c32d8101df003001d33f000021d0e894df008001d219f4b994bd71c2dd11c2dd71c32d11c32b03dd11c41884df00e00313e88048f0b890bd70819d10819d7081ed1081e987dd1082de0080c8f0b890bd70821d10821a07dd00821d10844d81e228804d5081bf9f22300124401'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 4),
    ('INT', 0),
    ('INT', 0),
    ('INT', 9),
    ('INT', 8),
    ('INT', 16777219),
    ('BYTES',
     '5e0995005500520000000000000000000000000000000000000055010500000055023a6700006d095b0300000000000000005509340153011a006a0855011300350000000000000035010000000000006e015b05000000000000000055025b06000000000000000035020000000000002800000000000000613d00005b080000000000000000520a00000000000000000000000000000000000022005820520c000000000000000000000000000000000000520e00000000000000000000000000000000000052100000000000000000000000000000000000005212000000000000000000000000000000000000530239026e024f395500520c000000000000000000000000000000000000520e000000000000000000000000000000000000521000000000000000000000000000000000000052120000000000000000000000000000000000005215000000000000000000000000000000000000550235010000000000006e025b05000000000000000055025b0800000000000000005216000000000000000000000000000000000000350200000000000028000000000000006105000055025409050000006e0255012800000000000000611300005501530305000000521800000000000000000000000000000000000053033a5800006101000067045500521b00000000000000000000000000000000000055015b080000000000000000521c0000000000000000000000000000000000002200550052000000000000000000000000000000000000005501050000005305530639025502340335010000000000002000582052000000000000000000000000000000000000005501270000005500521f00000000000000000000000000000000000035000000000000006e045b21000000000000000058145500520c0000000000000000000000000000000000005222000000000000000000000000000000000000522400000000000000000000000000000000000035030000000000006e055b05000000000000000055025b080000000000000000521600000000000000000000000000000000000035020000000000002800000000000000610f000055025b27000000000000000058143502000000000000050000006e064f0255026e065b050000000000000000550052000000000000000000000000000000000000005b080000000000000000521600000000000000000000000000000000000035020000000000002800000000000000645600005b0500000000000000005b290000000000000000550052000000000000000000000000000000000000005307530435030000000000005b080000000000000000521600000000000000000000000000000000000035020000000000002800000000000000642200005860520c000000000000000000000000000000000000520e00000000000000000000000000000000000052100000000000000000000000000000000000005505270000005b050000000000000000550052000000000000000000000000000000000000005b0800000000000000005216000000000000000000000000000000000000350200000000000028000000000000006434000055011300560773022f0073021300482700006e075b080000000000000000521c000000000000000000000000000000000000220055073501000000000000522b000000000000000000000000000000000000350000000000000050024d2900000b0020006e016e075b03000000000000000053081a0055011300350000000000000035010000000000006e085500522c00000000000000000000000000000000000053033a580000613c00005500522e0000000000000000000000000000000000005231000000000000000000000000000000000000550635010000000000005500520c000000000000000000000000000000000000520e000000000000000000000000000000000000523200000000000000000000000000000000000055052700000055005235000000000000000000000000000000000000550835010000000000002000550353054c00611100005500523700000000000000000000000000000000000035000000000000002000670467047302200073026e076600'),
    ('TUPLE',
     (('STR',
       'Set `indices` in `data` to `value`, while writing to edit history.\n'
       '\n'
       'Parameters\n'
       '----------\n'
       'indices : tuple of arrays of int\n'
       '    Indices in data to overwrite. Must be a tuple of arrays of length\n'
       '    equal to the number of data dimensions. (Fancy indexing in [2]_).\n'
       'value : int or array of int\n'
       '    New label value(s). If more than one value, must match or\n'
       '    broadcast with the given indices.\n'
       'refresh : bool, default True\n'
       '    whether to refresh the view, by default True\n'
       '\n'
       'References\n'
       '----------\n'
       '.. [2] https://numpy.org/doc/stable/user/basics.indexing.html\n'),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 3),
        ('INT', 16777267),
        ('BYTES', '3e0123002000950055001300480900006f115402050000007600950520004d0b00000b002000670037036601'),
        ('TUPLE', (('NONE',),)),
        ('TUPLE', ()),
        ('TUPLE', (('STR', '.0'), ('STR', 'x'))),
        ('TUPLE', (('STR', 'changed_indices'),)),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'Labels.data_setitem.<locals>.<genexpr>'),
        ('INT', 2105),
        ('BYTES', '83111401'),
        ('BYTES', 'f8e9008000d0173cb247a871982fd6182ab247f9'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))),
      ('TUPLE', (('STR', 'dtype'),)),
      ('INT', 0),
      ('NONE',),
      ('BOOL', True),
      ('TUPLE', (('STR', 'copy'),)),
      ('STR', 'data'),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 8),
        ('INT', 16777267),
        ('BYTES',
         '23002000950055001300484200006e015b0100000000000000005b03000000000000000055015205000000000000000000000000000000000000350000000000000035010000000000005b030000000000000000550152070000000000000000000000000000000000003500000000000000350100000000000053002d00000035020000000000007600950520004d4400000b002000670137036601'),
        ('TUPLE', (('INT', 1), ('NONE',))),
        ('TUPLE', (('STR', 'slice'), ('STR', 'int'), ('STR', 'min'), ('STR', 'max'))),
        ('TUPLE', (('STR', '.0'), ('STR', 'axis_indices'))),
        ('TUPLE', ()),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'Labels.data_setitem.<locals>.<genexpr>'),
        ('INT', 2155),
        ('BYTES', '82410a410c01'),
        ('BYTES',
         'e9008000f000031e0ae22027900cf403000d129423906cd71626d11626d31628d31229ac33a87cd72f3fd12f3fd32f41d32b42c051d12b46d70c47d00c47da2027f9'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py'))))),
    ('TUPLE',
     (('STR', 'data'),
      ('STR', 'tuple'),
      ('STR', 'isinstance'),
      ('STR', 'Sequence'),
      ('STR', 'np'),
      ('STR', 'asarray'),
      ('STR', '_slice'),
      ('STR', 'image'),
      ('STR', 'raw'),
      ('STR', 'dtype'),
      ('STR', 'type'),
      ('STR', 'ndarray'),
      ('STR', 'size'),
      ('STR', '_save_history'),
      ('STR', 'array'),
      ('STR', '_get_pt_not_disp'),
      ('STR', 'index_in_slice'),
      ('STR', 'slice_input'),
      ('STR', 'order'),
      ('STR', 'elements_in_slice'),
      ('STR', 'getattr'),
      ('STR', 'flatten'),
      ('STR', 'contour'),
      ('STR', 'colormap'),
      ('STR', '_data_to_texture'),
      ('STR', 'view'),
      ('STR', '_accumulate_updated_slice'),
      ('STR', '_partial_labels_refresh'))),
    ('TUPLE',
     (('STR', 'self'),
      ('STR', 'indices'),
      ('STR', 'value'),
      ('STR', 'refresh'),
      ('STR', 'pt_not_disp'),
      ('STR', 'displayed_indices'),
      ('STR', 'visible_values'),
      ('STR', 'x'),
      ('STR', 'updated_slice'))),
    ('TUPLE', ()),
    ('TUPLE', (('STR', 'changed_indices'),)),
    ('STR', 'data_setitem'),
    ('STR', 'Labels.data_setitem'),
    ('INT', 2086),
    ('BYTES', 'c90b2e4c0104'),
    ('BYTES',
     'f88000f024001b1f9f299929a047d11a2cb005d11a35880fdc1217d4173cb147d3173cd3123c8807e40b1590659c58d70b26d10b26dc1416974a924a9875af4ba94bd72c3dd12c3dd72c41d12c41d72c47d12c47d114488945e01418974b914bd71425d11425d71429d11429d7142fd1142fd71434d11434b055d3143b8845f406000c1690659c529f5a995ad70b28d10b28d81419982fd1142a8845e60f169827a021992a9f2f992fa851d31a2ed80c12e0080cd7081ad1081ae01017dc10129708920898149f199919a037d1192bb024d11037d81015f007040d0ef40306090af012001e238f0989099027d1081ae0161ad7162bd1162bd3162d880bdc1c2ad80c13a024a72ba12bd72239d12239d7223fd1223ff303021d0ad00819f406000c1690659c529f5a995ad70b28d10b28d81d22d42334b057d3234ad11d4b894ee01d22884ef406000d17907497799179a422a72aa12ad70c2dd10c2ddc0f199c27a024a729a129a856b054d31a3abc42bf4ab94ad70f47d10f47f00e003846018f4b894bd70c1dd10c1dd70c21d10c21d02233d10c34f40800101a98249f299929a452a75aa15ad70f30d10f30d9363dd3163eb267b011947297789278a001937bd7172ad1172ad6172cb1678847d0163ee4181df100031e0ae12027f305031e0af30003190a880df00a000c108f3c893c9831d30b1cf006001115970d910dd7102ed1102ea87ed3103ef003000d118f4b894bd70c1dd10c1dd70c22d10c22d02334d10c35f00800090dd70826d10826a07dd40835e00b1290648a3fd80c10d70c28d10c28d50c2af003000c1bf9f21f00173f'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\layers\\labels\\labels.py')),
   (('INT', 2),
    ('INT', 0),
    ('INT', 0),
    ('INT', 5),
    ('INT', 6),
    ('INT', 3),
    ('BYTES',
     '950055015201000000000000000000000000000000000000350000000000000013005602560373032f0073021300480c000075020000702358020500000055033a48000050024d0e00000b0020006e046e026e035b02000000000000000052040000000000000000000000000000000000005207000000000000000000000000000000000000550453015302390224007302200073036e036e026600'),
    ('TUPLE',
     (('STR',
       'Mask elements from a multi-dimensional index not in a given slice.\n'
       '\n'
       'Some n-D operations may edit data that is not visible in the current slice.\n'
       'Given slice position information (as a dictionary mapping axis to index on that\n'
       'axis), this function returns a boolean mask for the possibly higher-dimensional\n'
       'multi-index so that elements not currently visible are masked out. The\n'
       'resulting multi-index can then be subset and used to index into a texture or\n'
       'other lower-dimensional view.\n'
       '\n'
       'Parameters\n'
       '----------\n'
       'index : tuple of array of int\n'
       '    A NumPy fancy indexing expression [1]_.\n'
       'position_in_axes : dict[int, int]\n'
       '    A dictionary mapping sliced (non-displayed) axes to a slice position.\n'
       '\n'
       'Returns\n'
       '-------\n'
       'visible : array of bool\n'
       '    A boolean array indicating which items are visible in the current view.\n'),
      ('INT', 0),
      ('TUPLE', (('STR', 'axis'),)))),
    ('TUPLE', (('STR', 'items'), ('STR', 'np'), ('STR', 'logical_and'), ('STR', 'reduce'))),
    ('TUPLE',
     (('STR', 'index'), ('STR', 'position_in_axes'), ('STR', 'ax'), ('STR', 'position'), ('STR', 'queries'))),
    ('TUPLE', ()),
    ('TUPLE', ()),
    ('STR', 'elements_in_slice'),
    ('STR', 'elements_in_slice'),
    ('INT', 5),
    ('BYTES', '9413410806'),
    ('BYTES',
     '8000f03200334301d73248d13248d4324af403020f06da324aa12ca022880589099058d4081dd1324af00300050cf100020f06f406000c0e8f3e893ed70b20d10b20a017a871d00b20d00b31d00431f9f307020f06'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\utils\\_indexing.py')),
   (('INT', 3),
    ('INT', 0),
    ('INT', 0),
    ('INT', 3),
    ('INT', 5),
    ('INT', 3),
    ('BYTES',
     '5e005e015e0395005b0100000000000000005400540135020000000000006d035b030000000000000000550055035501340353011a006a0855021300350000000000000035010000000000002400'),
    ('TUPLE',
     (('STR',
       'Convert a NumPy fancy indexing expression from data to sliced space.\n'
       '\n'
       'Parameters\n'
       '----------\n'
       'index : tuple of array of int\n'
       '    A NumPy fancy indexing expression [1]_.\n'
       'position_in_axes : dict[int, int]\n'
       '    A dictionary mapping sliced (non-displayed) axes to a slice position.\n'
       'indices_order : tuple of int\n'
       '    The order of the indices in data view.\n'
       '\n'
       'Returns\n'
       '-------\n'
       'sliced_index : tuple of array of int\n'
       '    The indexing expression (nD) restricted to the current slice (usually\n'
       '    2D or 3D).\n'
       '\n'
       'Examples\n'
       '--------\n'
       '>>> index = (np.arange(5), np.full(5, 1), np.arange(4, 9))\n'
       '>>> index_in_slice(index, {0: 3}, (0, 1, 2))\n'
       '(array([1]), array([7]))\n'
       '>>> index_in_slice(index, {1: 1, 2: 8}, (0, 1, 2))\n'
       '(array([4]),)\n'
       '\n'
       'References\n'
       '----------\n'
       '[1]: https://numpy.org/doc/stable/user/basics.indexing.html#integer-array-indexing\n'),
      ('CODE',
       (('INT', 1),
        ('INT', 0),
        ('INT', 0),
        ('INT', 2),
        ('INT', 3),
        ('INT', 51),
        ('BYTES',
         '3e0323002000950055001300481500006e01550154043b010000640200004d0b000054025501050000005403050000007600950520004d1700000b002000670037036601'),
        ('TUPLE', (('NONE',),)),
        ('TUPLE', ()),
        ('TUPLE', (('STR', '.0'), ('STR', 'i'))),
        ('TUPLE', (('STR', 'index'), ('STR', 'index_in_slice'), ('STR', 'position_in_axes'))),
        ('TUPLE', ()),
        ('STR', '<genexpr>'),
        ('STR', 'index_in_slice.<locals>.<genexpr>'),
        ('INT', 70),
        ('BYTES', '830a2001910f2001'),
        ('BYTES', 'f8e9008000f000041106e2111e8841d80b0cd01424d10b24f305000921880588618908901ed60820da111ef9'),
        ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\utils\\_indexing.py'))))),
    ('TUPLE', (('STR', 'elements_in_slice'), ('STR', 'tuple'))),
    ('TUPLE', (('STR', 'index'), ('STR', 'position_in_axes'), ('STR', 'indices_order'))),
    ('TUPLE', ()),
    ('TUPLE', (('STR', 'index'), ('STR', 'position_in_axes'), ('STR', 'index_in_slice'))),
    ('STR', 'index_in_slice'),
    ('STR', 'index_in_slice'),
    ('INT', 35),
    ('BYTES', ''),
    ('BYTES', 'fa8000f44401001627a075d02e3ed3153f804edc0b10f600041106e1111ef305041106f300040c06f000040506'),
    ('STR', 'C:\\Users\\HEQ\\Projects\\naparixx\\src\\napari\\utils\\_indexing.py')))))

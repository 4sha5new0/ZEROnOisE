package com.example.audioplayer.ui;

import android.app.AlertDialog;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.ListView;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import com.example.audioplayer.AudioInfoData;
import com.example.audioplayer.FileEntryData;
import com.example.audioplayer.MainActivity;
import com.example.audioplayer.PlayerService;
import com.example.audioplayer.R;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public class BrowserFragment extends Fragment {

    private static final String ARG_DIR = "dir_path";

    private PlayerService  svc;
    private String         currentDir;
    private FileEntryData[] entries;

    private TextView    tvPath;
    private ListView    listView;
    private TextView    tvRootInternal, tvRootSd;

    public static BrowserFragment newInstance(@Nullable String dirPath) {
        final BrowserFragment f = new BrowserFragment();
        final Bundle args = new Bundle();
        if (dirPath != null) args.putString(ARG_DIR, dirPath);
        f.setArguments(args);
        return f;
    }

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
            @Nullable ViewGroup container, @Nullable Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_browser, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View root, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(root, savedInstanceState);

        tvPath       = root.findViewById(R.id.tv_current_path);
        listView     = root.findViewById(R.id.list_view);
        tvRootInternal = root.findViewById(R.id.btn_root_internal);
        tvRootSd     = root.findViewById(R.id.btn_root_sd);

        if (getActivity() instanceof MainActivity)
            svc = ((MainActivity) getActivity()).service;

        // ストレージルートボタン
        if (svc != null) {
            final String[] roots = svc.bridge.nativeGetStorageRoots();
            if (roots != null && roots.length > 0) {
                tvRootInternal.setText(R.string.internal_storage);
                tvRootInternal.setOnClickListener(v -> navigateTo(roots[0]));
                tvRootInternal.setVisibility(View.VISIBLE);
                if (roots.length > 1) {
                    tvRootSd.setText(R.string.sd_card);
                    tvRootSd.setOnClickListener(v -> navigateTo(roots[1]));
                    tvRootSd.setVisibility(View.VISIBLE);
                }
            }
        }

        // 引数のディレクトリへ移動（null なら初回はストレージルートへ）
        final String argDir = getArguments() != null
            ? getArguments().getString(ARG_DIR) : null;
        if (argDir != null) {
            navigateTo(argDir);
        } else if (svc != null) {
            final String[] roots = svc.bridge.nativeGetStorageRoots();
            if (roots != null && roots.length > 0) navigateTo(roots[0]);
        }
    }

    private void navigateTo(String dirPath) {
        if (svc == null || dirPath == null) return;
        currentDir = dirPath;
        tvPath.setText(dirPath);

        // スキャンは UI スレッドで行う（M3 Ultra は SSD なので十分高速）
        entries = svc.bridge.nativeScanDirectory(dirPath);

        // 表示リストを構築（上の階層 → エントリ一覧）
        final List<String> labels = new ArrayList<>();
        final List<String> subs   = new ArrayList<>();

        // ".." 上の階層
        final int sep = dirPath.lastIndexOf('/');
        if (sep > 0) {
            labels.add("📂  ..");
            subs.add("上の階層");
        } else {
            labels.add(null);
            subs.add(null);
        }

        for (FileEntryData fe : entries) {
            if (fe.isDirectory) {
                labels.add("📂  " + fe.name);
                subs.add("");
            } else {
                labels.add("🎵  " + fe.name);
                subs.add(fe.getSubLabel());
            }
        }

        // 2行カスタムアダプター（name + subLabel）
        final ArrayAdapter<String> adapter = new ArrayAdapter<String>(
                requireContext(), R.layout.item_file, R.id.item_name, labels) {
            @NonNull
            @Override
            public View getView(int position, @Nullable View convertView,
                    @NonNull ViewGroup parent) {
                final View v = super.getView(position, convertView, parent);
                final TextView sub = v.findViewById(R.id.item_sub);
                if (sub != null && position < subs.size())
                    sub.setText(subs.get(position) != null ? subs.get(position) : "");
                return v;
            }
        };
        listView.setAdapter(adapter);

        // タップ: ディレクトリなら移動、ファイルなら再生
        listView.setOnItemClickListener((parent, view, position, id) -> {
            if (position == 0 && sep > 0) {
                // 上の階層
                navigateTo(dirPath.substring(0, sep));
                return;
            }
            final int idx = position - 1;  // ".." の分オフセット
            if (idx < 0 || idx >= entries.length) return;
            final FileEntryData fe = entries[idx];
            if (fe.isDirectory) {
                if (getActivity() instanceof MainActivity)
                    ((MainActivity) getActivity()).showBrowser(fe.fullPath);
            } else {
                openAndPlay(fe.fullPath);
            }
        });

        // 長押し: 詳細情報ダイアログ
        listView.setOnItemLongClickListener((parent, view, position, id) -> {
            final int idx = position - 1;
            if (idx < 0 || idx >= entries.length || entries[idx].isDirectory)
                return false;
            showDetailDialog(entries[idx]);
            return true;
        });
    }

    private void openAndPlay(String path) {
        if (svc == null) return;
        if (svc.bridge.nativeLoadFile(svc.engineHandle, path)) {
            svc.bridge.nativePlay(svc.engineHandle);
            if (getActivity() instanceof MainActivity)
                ((MainActivity) getActivity()).showPlayer();
        }
    }

    private void showDetailDialog(FileEntryData fe) {
        final AudioInfoData info = fe.audioInfo != null
            ? fe.audioInfo
            : (svc != null ? svc.bridge.nativeReadMetadata(fe.fullPath) : null);
        if (info == null) return;

        final StringBuilder sb = new StringBuilder();
        sb.append("ファイル: ").append(fe.name).append("\n\n");
        sb.append("フォーマット: ").append(info.getFormatName()).append("\n");
        if (info.isLossless) {
            sb.append("サンプルレート: ").append(info.getSampleRateLabel()).append("\n");
            sb.append("ビット深度: ").append(info.bitDepth).append("bit\n");
        } else {
            sb.append("ビットレート: ").append(info.bitrateKbps).append(" kbps\n");
        }
        sb.append("チャンネル: ").append(info.channels).append("\n");
        sb.append("時間: ").append(info.getDurationLabel()).append("\n");
        sb.append("サイズ: ").append(info.getFileSizeLabel()).append("\n");
        if (info.isMqa) {
            sb.append("\nMQA: ").append(info.getMqaBadgeText()).append("\n");
            if (!info.mqaEncoder.isEmpty())
                sb.append("エンコーダー: ").append(info.mqaEncoder).append("\n");
        }
        if (!info.title.isEmpty())  sb.append("\nタイトル: ").append(info.title);
        if (!info.artist.isEmpty()) sb.append("\nアーティスト: ").append(info.artist);
        if (!info.album.isEmpty())  sb.append("\nアルバム: ").append(info.album);

        new AlertDialog.Builder(requireContext())
            .setTitle(fe.name)
            .setMessage(sb.toString())
            .setPositiveButton("再生", (d, w) -> openAndPlay(fe.fullPath))
            .setNegativeButton("閉じる", null)
            .show();
    }
}

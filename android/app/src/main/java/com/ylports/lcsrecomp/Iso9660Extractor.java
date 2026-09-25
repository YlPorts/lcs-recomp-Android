package com.ylports.lcsrecomp;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.channels.FileChannel;
import java.util.HashSet;
import java.util.Set;

final class Iso9660Extractor {
    static final int SECTOR_SIZE = 2048;

    interface Progress {
        void onFile(String path, long extractedBytes);
    }

    static final class Result {
        final String volumeId;
        final long extractedBytes;

        Result(String volumeId, long extractedBytes) {
            this.volumeId = volumeId;
            this.extractedBytes = extractedBytes;
        }
    }

    private static final class Entry {
        final long lba;
        final long size;
        final boolean directory;
        final String name;

        Entry(long lba, long size, boolean directory, String name) {
            this.lba = lba;
            this.size = size;
            this.directory = directory;
            this.name = name;
        }
    }

    private final FileChannel channel;
    private final File root;
    private final Progress progress;
    private final Set<String> visitedDirectories = new HashSet<>();
    private long extractedBytes;

    private Iso9660Extractor(FileChannel channel, File root, Progress progress) {
        this.channel = channel;
        this.root = root;
        this.progress = progress;
    }

    static Result extract(FileChannel channel, File root, Progress progress) throws IOException {
        if (channel == null) throw new IOException("No se pudo abrir el ISO.");
        if (!root.exists() && !root.mkdirs())
            throw new IOException("No se pudo crear la carpeta de extracción.");

        Iso9660Extractor extractor = new Iso9660Extractor(channel, root, progress);
        byte[] pvd = extractor.readBytes(16L * SECTOR_SIZE, SECTOR_SIZE);

        if ((pvd[0] & 0xFF) != 1
                || pvd[1] != 'C' || pvd[2] != 'D' || pvd[3] != '0'
                || pvd[4] != '0' || pvd[5] != '1') {
            throw new IOException("El archivo no parece ser un ISO de PSP/ISO9660 válido.");
        }

        String volumeId = new String(pvd, 40, 32, StandardCharsets.US_ASCII).trim();
        int rootLength = pvd[156] & 0xFF;
        if (rootLength < 34 || 156 + rootLength > pvd.length)
            throw new IOException("Directorio raíz ISO9660 inválido.");

        Entry rootEntry = extractor.parseRecord(pvd, 156);
        if (!rootEntry.directory)
            throw new IOException("El ISO no tiene un directorio raíz válido.");

        extractor.extractDirectory(rootEntry, root, "", 0);
        return new Result(volumeId, extractor.extractedBytes);
    }

    private void extractDirectory(Entry directory, File destination,
                                  String relativePath, int depth) throws IOException {
        if (depth > 64) throw new IOException("El ISO tiene demasiados niveles de carpetas.");

        String visitKey = directory.lba + ":" + directory.size;
        if (!visitedDirectories.add(visitKey)) return;

        if (directory.size < 0 || directory.size > 64L * 1024L * 1024L)
            throw new IOException("Directorio ISO9660 demasiado grande.");

        byte[] data = readBytes(directory.lba * SECTOR_SIZE, (int) directory.size);
        int offset = 0;
        while (offset < data.length) {
            int recordLength = data[offset] & 0xFF;
            if (recordLength == 0) {
                int nextSector = ((offset / SECTOR_SIZE) + 1) * SECTOR_SIZE;
                if (nextSector <= offset) break;
                offset = nextSector;
                continue;
            }
            if (recordLength < 34 || offset + recordLength > data.length)
                throw new IOException("Registro ISO9660 dañado.");

            int nameLength = data[offset + 32] & 0xFF;
            if (33 + nameLength > recordLength)
                throw new IOException("Nombre ISO9660 inválido.");

            int firstNameByte = nameLength == 0 ? -1 : data[offset + 33] & 0xFF;
            if (!(nameLength == 1 && (firstNameByte == 0 || firstNameByte == 1))) {
                Entry entry = parseRecord(data, offset);
                String safeName = sanitizeName(entry.name);
                if (!safeName.isEmpty()) {
                    File output = new File(destination, safeName);
                    ensureInsideRoot(output);

                    String childPath = relativePath.isEmpty()
                            ? safeName : relativePath + "/" + safeName;

                    if (entry.directory) {
                        if (!output.exists() && !output.mkdirs())
                            throw new IOException("No se pudo crear " + childPath);
                        extractDirectory(entry, output, childPath, depth + 1);
                    } else {
                        extractFile(entry, output, childPath);
                    }
                }
            }
            offset += recordLength;
        }
    }

    private Entry parseRecord(byte[] source, int offset) throws IOException {
        int recordLength = source[offset] & 0xFF;
        if (recordLength < 34 || offset + recordLength > source.length)
            throw new IOException("Registro ISO9660 inválido.");

        long lba = readLe32(source, offset + 2);
        long size = readLe32(source, offset + 10);
        boolean directory = (source[offset + 25] & 0x02) != 0;

        int nameLength = source[offset + 32] & 0xFF;
        String name = "";
        if (nameLength > 0 && offset + 33 + nameLength <= source.length) {
            int first = source[offset + 33] & 0xFF;
            if (!(nameLength == 1 && (first == 0 || first == 1))) {
                name = new String(source, offset + 33, nameLength,
                        StandardCharsets.US_ASCII);
                int version = name.indexOf(';');
                if (version >= 0) name = name.substring(0, version);
                while (name.endsWith(".")) name = name.substring(0, name.length() - 1);
            }
        }
        return new Entry(lba, size, directory, name);
    }

    private void extractFile(Entry entry, File output, String relativePath) throws IOException {
        File parent = output.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs())
            throw new IOException("No se pudo crear la carpeta de " + relativePath);

        long sourceOffset = entry.lba * SECTOR_SIZE;
        long remaining = entry.size;
        ByteBuffer buffer = ByteBuffer.allocate(1024 * 1024);

        try (FileOutputStream out = new FileOutputStream(output)) {
            while (remaining > 0) {
                buffer.clear();
                int wanted = (int) Math.min((long) buffer.capacity(), remaining);
                buffer.limit(wanted);

                int total = 0;
                while (total < wanted) {
                    int read = channel.read(buffer, sourceOffset + total);
                    if (read < 0) throw new IOException("Fin inesperado del ISO.");
                    if (read == 0) throw new IOException("No se pudo continuar leyendo el ISO.");
                    total += read;
                }

                out.write(buffer.array(), 0, total);
                sourceOffset += total;
                remaining -= total;
                extractedBytes += total;
            }
        }

        if (progress != null) progress.onFile(relativePath, extractedBytes);
    }

    private byte[] readBytes(long offset, int size) throws IOException {
        if (size < 0) throw new IOException("Tamaño ISO inválido.");
        ByteBuffer buffer = ByteBuffer.allocate(size);
        int total = 0;
        while (total < size) {
            int read = channel.read(buffer, offset + total);
            if (read < 0) throw new IOException("Fin inesperado del ISO.");
            if (read == 0) throw new IOException("No se pudo leer el ISO.");
            total += read;
        }
        return buffer.array();
    }

    private void ensureInsideRoot(File file) throws IOException {
        String rootPath = root.getCanonicalPath();
        String filePath = file.getCanonicalPath();
        if (!filePath.equals(rootPath)
                && !filePath.startsWith(rootPath + File.separator)) {
            throw new IOException("Ruta inválida dentro del ISO.");
        }
    }

    private static String sanitizeName(String name) {
        if (name == null) return "";
        String clean = name.replace('/', '_').replace('\\', '_').replace('\0', '_').trim();
        if (clean.equals(".") || clean.equals("..")) return "";
        return clean;
    }

    private static long readLe32(byte[] data, int offset) {
        return ((long) data[offset] & 0xFFL)
                | (((long) data[offset + 1] & 0xFFL) << 8)
                | (((long) data[offset + 2] & 0xFFL) << 16)
                | (((long) data[offset + 3] & 0xFFL) << 24);
    }
}

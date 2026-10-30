package org.kppmods.antidox;

import java.io.*;
import java.util.*;

/** Change only the Java Settings display calls used by Antidox. */
public final class Patches {
    private static final String DISPLAY = "org/kppmods/antidox/Display";
    private final byte[] source;
    private final DataInputStream input;
    private final List<byte[]> pool = new ArrayList<byte[]>();
    private final Set<Integer> staticCalls = new HashSet<Integer>();

    private Patches(byte[] source) throws IOException {
        this.source = source;
        input = new DataInputStream(new ByteArrayInputStream(source));
        if (input.readInt() != 0xcafebabe) throw new IOException("Invalid class");
        input.readInt();
        int count = input.readUnsignedShort();
        pool.add(null);
        for (int i = 1; i < count; i++) {
            int start = source.length - input.available();
            int tag = input.readUnsignedByte();
            switch (tag) {
                case 1: input.readUTF(); break;
                case 3: case 4: input.skipBytes(4); break;
                case 5: case 6: input.skipBytes(8); break;
                case 7: case 8: case 16: case 19: case 20: input.skipBytes(2); break;
                case 9: case 10: case 11: case 12: case 17: case 18: input.skipBytes(4); break;
                case 15: input.skipBytes(3); break;
                default: throw new IOException("Unsupported constant " + tag);
            }
            pool.add(Arrays.copyOfRange(source, start, source.length - input.available()));
            if (tag == 5 || tag == 6) { pool.add(null); i++; }
        }
    }

    private static int u2(byte[] data, int offset) {
        return (data[offset] & 255) << 8 | data[offset + 1] & 255;
    }

    private static int i4(byte[] data, int offset) {
        return u2(data, offset) << 16 | u2(data, offset + 2);
    }

    private String utf(int index) throws IOException {
        return new DataInputStream(new ByteArrayInputStream(pool.get(index), 1,
            pool.get(index).length - 1)).readUTF();
    }

    private String owner(int index) throws IOException {
        return utf(u2(pool.get(u2(pool.get(index), 1)), 1));
    }

    private String name(int index) throws IOException {
        return utf(u2(pool.get(u2(pool.get(index), 3)), 1));
    }

    private String descriptor(int index) throws IOException {
        return utf(u2(pool.get(u2(pool.get(index), 3)), 3));
    }

    private int add(byte[] entry) { pool.add(entry); return pool.size() - 1; }

    private int utfEntry(String text) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        DataOutputStream out = new DataOutputStream(bytes);
        out.writeByte(1); out.writeUTF(text);
        return add(bytes.toByteArray());
    }

    private static byte[] entry(int tag, int first, int second) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        DataOutputStream out = new DataOutputStream(bytes);
        out.writeByte(tag); out.writeShort(first);
        if (second >= 0) out.writeShort(second);
        return bytes.toByteArray();
    }

    private byte[] reference(String owner, String name, String desc) throws IOException {
        int cls = add(entry(7, utfEntry(owner), -1));
        int type = add(entry(12, utfEntry(name), utfEntry(desc)));
        return entry(10, cls, type);
    }

    private byte[] attributes(boolean patchCode) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        DataOutputStream out = new DataOutputStream(bytes);
        int count = input.readUnsignedShort();
        out.writeShort(count);
        for (int i = 0; i < count; i++) {
            int name = input.readUnsignedShort(), length = input.readInt();
            if (length < 0 || length > input.available()) throw new IOException("Invalid attribute");
            byte[] data = new byte[length]; input.readFully(data);
            if (patchCode && "Code".equals(utf(name))) patchCalls(data);
            out.writeShort(name); out.writeInt(length); out.write(data);
        }
        return bytes.toByteArray();
    }

    // Replacing an instance display getter with a static helper keeps the
    // operand stack and all branch offsets unchanged.
    private void patchCalls(byte[] data) throws IOException {
        int end = 8 + i4(data, 4);
        for (int pos = 8; pos < end;) {
            int op = data[pos] & 255, size = 1;
            if (op == 182 && staticCalls.contains(Integer.valueOf(u2(data, pos + 1)))) data[pos] = (byte) 184;
            if (op == 170 || op == 171) {
                int aligned = pos + 1;
                while ((aligned - 8) % 4 != 0) aligned++;
                size = aligned - pos + (op == 170
                    ? 12 + 4 * (i4(data, aligned + 8) - i4(data, aligned + 4) + 1)
                    : 8 + 8 * i4(data, aligned + 4));
            } else if (op == 196) size = (data[pos + 1] & 255) == 132 ? 6 : 4;
            else if (op == 185 || op == 186 || op == 200 || op == 201) size = 5;
            else if (op == 197) size = 4;
            else if (op == 17 || op == 19 || op == 20 || op == 132 ||
                (op >= 153 && op <= 168) || (op >= 178 && op <= 184) ||
                op == 187 || op == 189 || op == 192 || op == 193 || op == 198 || op == 199) size = 3;
            else if (op == 16 || op == 18 || (op >= 21 && op <= 25) ||
                (op >= 54 && op <= 58) || op == 169 || op == 188) size = 2;
            if (size < 1 || pos + size > end) throw new IOException("Invalid instruction");
            pos += size;
        }
    }

    private byte[] method(int access, String name, String desc, byte[] code,
                          int locals, int frame) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream(), body = new ByteArrayOutputStream();
        DataOutputStream out = new DataOutputStream(bytes), b = new DataOutputStream(body);
        b.writeShort(2); b.writeShort(locals); b.writeInt(code.length); b.write(code); b.writeShort(0);
        b.writeShort(frame < 0 ? 0 : 1);
        if (frame >= 0) {
            b.writeShort(utfEntry("StackMapTable")); b.writeInt(3);
            b.writeShort(1); b.writeByte(frame);
        }
        out.writeShort(access); out.writeShort(utfEntry(name)); out.writeShort(utfEntry(desc));
        out.writeShort(1); out.writeShort(utfEntry("Code")); out.writeInt(body.size()); out.write(body.toByteArray());
        return bytes.toByteArray();
    }

    public static byte[] transform(byte[] source) throws IOException {
        return new Patches(source).adapt();
    }

    private byte[] adapt() throws IOException {
        int access = input.readUnsignedShort(), self = input.readUnsignedShort(), parent = input.readUnsignedShort();
        String cls = utf(u2(pool.get(self), 1));
        boolean info = cls.equals("com/amazon/kindle/settings/dialog/DeviceInfoDialog");
        boolean saver = cls.equals("com/amazon/kindle/settings/dialog/DeviceNameDialog$a");
        boolean wifi = cls.equals("com/amazon/kindle/settings/view/settings/WirelessAndBluetoothDisplayController");
        boolean names = cls.equals("com/amazon/kindle/settings/dialog/DeviceNameDialog") ||
            cls.equals("com/amazon/kindle/settings/view/personalize/PersonalizeDisplayController") ||
            cls.equals("com/amazon/kindle/settings/view/personalize/DeviceNameAndPersonalInfoDisplayController") ||
            cls.equals("com/amazon/kindle/settings/view/settings/SettingsDisplayController");
        if (!info && !saver && !names && !wifi) return null;
        int originalSetter = 0, changes = 0;
        String setterDesc = null;
        int originalPoolSize = pool.size();
        for (int i = 1; i < originalPoolSize; i++) {
            byte[] item = pool.get(i);
            if (item == null || item[0] != 10) continue;
            String owner = owner(i), name = name(i), desc = descriptor(i);
            if (info && owner.equals("com/amazon/ebook/util/text/format/MessageFormat") &&
                name.equals("format") && desc.equals("(Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;")) {
                pool.set(i, reference(DISPLAY, "formatInfo", desc)); changes++;
            } else if (names && owner.equals("com/amazon/kindle/apps/util/CommunicationUtils") &&
                (name.equals("d") || name.equals("f")) &&
                desc.equals("(Lcom/amazon/kindle/booklet/Booklet;)Ljava/lang/String;")) {
                pool.set(i, reference(DISPLAY, "hidden", "(Ljava/lang/Object;)Ljava/lang/String;")); changes++;
            } else if (wifi && owner.equals("com/amazon/kindle/restricted/net/wifi/WiFiNetwork") &&
                name.equals("cWO") && desc.equals("()Ljava/lang/String;")) {
                pool.set(i, reference(DISPLAY, "hidden", "(Ljava/lang/Object;)Ljava/lang/String;"));
                staticCalls.add(Integer.valueOf(i)); changes++;
            } else if ((names || wifi) && owner.equals("java/lang/String") && name.equals("toUpperCase") &&
                desc.equals("()Ljava/lang/String;")) {
                pool.set(i, reference(DISPLAY, "upper", "(Ljava/lang/String;)Ljava/lang/String;"));
                staticCalls.add(Integer.valueOf(i));
            } else if (saver && owner.equals("com/amazon/kindle/apps/util/CommunicationUtils") &&
                name.equals("b") && desc.equals("(Lcom/amazon/kindle/booklet/Booklet;Ljava/lang/String;)V")) {
                originalSetter = add(item); setterDesc = desc;
                pool.set(i, reference(cls, "antidox$saveName", desc)); changes++;
            }
        }
        if (changes == 0) throw new IOException("No Antidox display call in " + cls);
        ByteArrayOutputStream body = new ByteArrayOutputStream();
        DataOutputStream out = new DataOutputStream(body);
        out.writeShort(access); out.writeShort(self); out.writeShort(parent);
        int interfaces = input.readUnsignedShort(); out.writeShort(interfaces);
        for (int i = 0; i < interfaces; i++) out.writeShort(input.readUnsignedShort());
        int fields = input.readUnsignedShort(); out.writeShort(fields);
        for (int i = 0; i < fields; i++) {
            out.writeShort(input.readUnsignedShort()); out.writeShort(input.readUnsignedShort());
            out.writeShort(input.readUnsignedShort()); out.write(attributes(false));
        }
        int methods = input.readUnsignedShort();
        out.writeShort(methods + (info ? 1 : 0) + (originalSetter != 0 ? 1 : 0));
        boolean wrapped = false;
        for (int i = 0; i < methods; i++) {
            int flags = input.readUnsignedShort(), name = input.readUnsignedShort(), desc = input.readUnsignedShort();
            boolean wrap = info && utf(name).equals("initGUI") && utf(desc).equals("()V");
            out.writeShort(flags); out.writeShort(wrap ? utfEntry("antidox$initGUI") : name);
            out.writeShort(desc); out.write(attributes(true));
            if (wrap) wrapped = true;
        }
        if (info) {
            if (!wrapped) throw new IOException("DeviceInfoDialog initGUI is absent");
            int stock = add(reference(cls, "antidox$initGUI", "()V"));
            int display = add(reference(DISPLAY, "deviceInfo", "(Ljava/lang/Object;)V"));
            out.write(method(2, "initGUI", "()V", new byte[] {
                42, (byte)183, (byte)(stock >> 8), (byte)stock,
                42, (byte)184, (byte)(display >> 8), (byte)display, (byte)177
            }, 1, -1));
        }
        if (originalSetter != 0) {
            int allowed = add(reference(DISPLAY, "saveAllowed", "(Ljava/lang/String;)Z"));
            out.write(method(10, "antidox$saveName", setterDesc, new byte[] {
                43, (byte)184, (byte)(allowed >> 8), (byte)allowed,
                (byte)153, 0, 8, 42, 43,
                (byte)184, (byte)(originalSetter >> 8), (byte)originalSetter, (byte)177
            }, 2, 12));
        }
        out.write(attributes(false));
        if (input.available() != 0) throw new IOException("Trailing class bytes");
        ByteArrayOutputStream result = new ByteArrayOutputStream();
        DataOutputStream complete = new DataOutputStream(result);
        complete.write(source, 0, 8); complete.writeShort(pool.size());
        for (byte[] item : pool) if (item != null) complete.write(item);
        complete.write(body.toByteArray());
        return result.toByteArray();
    }
}

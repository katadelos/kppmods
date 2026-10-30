package org.kppmods.antidox;

import java.awt.*;
import java.awt.event.*;
import java.lang.reflect.*;
import java.util.*;
import javax.swing.*;

/** Display-only changes; stock identity and registration services keep their values. */
public final class Display implements ActionListener {
    private static final String TOKEN = "KPPMODS_ANTIDOX_";
    private static final ThreadLocal<Object[]> details = new ThreadLocal<Object[]>();
    private final JButton button;
    private final String value;

    private Display(JButton button, String value) {
        this.button = button; this.value = value;
    }

    public static String hidden(Object ignored) { return "hidden"; }
    public static String upper(String value) {
        return "hidden".equals(value) ? value : value.toUpperCase();
    }
    public static boolean saveAllowed(String value) { return !"hidden".equals(value); }

    public void actionPerformed(ActionEvent event) {
        button.setText(value.equals(button.getText()) ? "Press to show" : value);
    }

    public static String formatInfo(String template, Object[] values) throws Exception {
        Object[] display = values;
        if (values.length == 8 && template.indexOf("{7}") >= 0) {
            details.set(values.clone());
            display = values.clone();
            display[1] = TOKEN + "1"; display[2] = TOKEN + "2"; display[6] = TOKEN + "6";
        }
        Class<?> formatter = Class.forName("com.amazon.ebook.util.text.format.MessageFormat");
        try {
            return (String) formatter.getMethod("format", String.class, Object[].class)
                .invoke(null, template, display);
        } catch (InvocationTargetException failure) {
            throw new IllegalStateException("Cannot format device details", failure.getCause());
        }
    }

    private static Object call(Object object, String method) throws Exception {
        return object.getClass().getMethod(method).invoke(object);
    }

    private static Component textView(Container parent) {
        for (Component child : parent.getComponents()) {
            if (child.getClass().getName().equals("com.amazon.ebook.booklet.reader.sdk.ui.html.MiddleClassTextView")) return child;
            if (child instanceof Container) {
                Component result = textView((Container) child);
                if (result != null) return result;
            }
        }
        return null;
    }

    private static String text(String html) {
        return html.replaceAll("<[^>]*>", "").replace("&nbsp;", " ")
            .replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">")
            .replace("&quot;", "\"").replace("&#39;", "'").trim();
    }

    public static void deviceInfo(Object dialog) throws Exception {
        Object[] values = details.get(); details.remove();
        if (values == null) throw new IllegalStateException("Device details were not captured");
        Container content = (Container) call(dialog, "getContentPane");
        Component original = textView(content);
        if (original == null) throw new IllegalStateException("Device details view is absent");
        String html = (String) call(original, "getText");
        ResourceBundle resources = ResourceBundle.getBundle("com.amazon.kindle.settings.resources.DialogResources");
        Font font = (Font) resources.getObject("deviceinfo.dialog.font");
        Insets margins = (Insets) resources.getObject("deviceinfo.dialog.margins");
        final java.util.List<Display> buttons = new ArrayList<Display>();
        int lineSpacing = ((Integer) resources.getObject("deviceinfo.dialog.linespacing")).intValue();
        int rowHeight = original.getFontMetrics(font).getHeight() + lineSpacing;
        JPanel rows = new JPanel(new GridLayout(0, 1, 0, 0));
        rows.setBorder(BorderFactory.createEmptyBorder(margins.top, margins.left, margins.bottom, margins.right));
        rows.setBackground(Color.WHITE);
        for (String line : html.split("(?i)<br\\s*/?>")) {
            int labelEnd = line.indexOf("</b>");
            String label = labelEnd >= 0 ? text(line.substring(0, labelEnd)) : "";
            String value = text(labelEnd >= 0 ? line.substring(labelEnd + 4) : line);
            JPanel row = new JPanel(new BorderLayout(font.getSize() / 2, 0));
            row.setPreferredSize(new Dimension(0, rowHeight));
            row.setBackground(Color.WHITE);
            JLabel title = new JLabel(label.isEmpty() ? label : label + " "); title.setFont(new Font(font.getName(), Font.BOLD, font.getSize()));
            row.add(title, BorderLayout.WEST);
            if (value.startsWith(TOKEN)) {
                int index = Integer.parseInt(value.substring(TOKEN.length()));
                JButton button = new JButton("Press to show"); button.setFont(font);
                button.setMargin(new Insets(0, 0, 0, 0));
                button.setPreferredSize(new Dimension(0, rowHeight));
                button.setName("antidox-device-info-" + index);
                Display listener = new Display(button, String.valueOf(values[index]));
                button.addActionListener(listener); buttons.add(listener);
                row.add(button, BorderLayout.CENTER);
            } else {
                JLabel detail = new JLabel(value); detail.setFont(font);
                row.add(detail, BorderLayout.CENTER);
            }
            rows.add(row);
        }
        content.removeAll(); content.add(rows, BorderLayout.NORTH);
        ((Component) dialog).addComponentListener(new ComponentAdapter() {
            public void componentHidden(ComponentEvent event) {
                for (Display item : buttons) item.button.setText("Press to show");
            }
        });
        content.validate();
    }
}
